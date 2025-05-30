# Copyright 2022 The TensorStore Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Functions to assist in invoking protoc for CMake.

proto_library() and friends involve some compromises between Bazel and CMake
target handling.  In Bazel there is a global view of the dependency tree,
which allows aspects to extract information about dependencies to ensure
that dependencies in various compilation modes are correctly expressed;
bazel_to_cmake needs to allow for that in compilation and target generation.

The bazel_to_cmake translation of proto_library creates an INTERFACE library
with the proto sources along with INTERFACE_INCLUDES for the include paths
and INTERFACE_LINK_LIBRARIES for the dependencies.  These libraries are not
typical C/C++ libraries and should not be linked as such, but can provide
a global view of dependencies.

The dependencies are queried by the btc_protobuf cmake macros, which,
given a TARGET and a PROTO_TARGET, will compile the .proto sources present
in the PROTO_TARGET and add them as generated files to the TARGET.

Note that the mechanism described above is quite a bit different from the
typical FindProtobuf variant of the CMake protobuf rules.

Also, since bazel_to_cmake does not include the whole "aspect" mechanism
to ensure that dependencies have corresponding rules, the various protobuf
output rules are expected to have somewhat standard rule names. See
cc_proto_library for more details.

This generator assumes that appropriate mappings between bazel and CMake targets
have been configured:
  @com_google_protobuf//:protobuf      => protobuf::libprotobuf
  @com_google_protobuf//:protobuf_lite => protobuf::libprotobuf-lite
  @com_google_protobuf//:protoc        => protobuf::protoc
  @com_google_protobuf//:protoc_lib    => protobuf::libprotoc

In debian-like systems, use of system versions requires the following packages:
  apt install libprotobuf-dev libprotoc-dev protobuf-compiler

Note that when using system protobuf, the well_known_proto_types are available
via protobuf::libprotobuf. For reference, see

Bazel rules related to protobuf for reference:
https://github.com/bazelbuild/bazel/tree/master/src/main/starlark/builtins_bzl/common/proto/proto_library.bzl
https://github.com/bazelbuild/bazel/tree/master/src/main/starlark/builtins_bzl/common/cc/cc_proto_library.bzl
https://github.com/bazelbuild/rules_proto/tree/master/proto
"""

# pylint: disable=invalid-name

import io
from typing import List, Optional

from .cmake_builder import CMakeBuilder
from .cmake_provider import default_providers
from .emit_cc import construct_cc_includes
from .emit_filegroup import emit_filegroup
from .evaluation import EvaluationState
from .native_aspect import invoke_proto_aspects
from .starlark.bazel_target import RepositoryId
from .starlark.bazel_target import TargetId
from .starlark.common_providers import FilesProvider
from .starlark.common_providers import ProtoLibraryProvider
from .starlark.invocation_context import InvocationContext
from .starlark.label import RelativeLabel
from .starlark.provider import TargetInfo
from .starlark.scope_build_file import register_native_build_rule

PROTO_REPO = RepositoryId("com_google_protobuf")

_SEP = "\n        "


@register_native_build_rule
def proto_library(
    self: InvocationContext,
    name: str,
    visibility: Optional[List[RelativeLabel]] = None,
    **kwargs,
):
  context = self.snapshot()
  target = context.parse_rule_target(name)
  context.add_rule(
      target,
      lambda: _proto_library_impl(context, target, **kwargs),
      visibility=visibility,
  )
  invoke_proto_aspects(context, target, visibility)


def _proto_library_impl(
    _context: InvocationContext,
    _target: TargetId,
    srcs: Optional[List[RelativeLabel]] = None,  # .proto sources
    deps: Optional[List[RelativeLabel]] = None,  # proto_libraries
    strip_import_prefix: Optional[str] = None,
    import_prefix: Optional[str] = None,
    **kwargs,
):
  del kwargs
  resolved_srcs = _context.resolve_target_or_label_list(
      _context.evaluate_configurable_list(srcs)
  )
  resolved_deps = _context.resolve_target_or_label_list(
      _context.evaluate_configurable_list(deps)
  )

  state = _context.access(EvaluationState)
  cmake_target_pair = state.generate_cmake_target_pair(_target)

  import_var: str = ""

  def maybe_set_import_var(d: TargetId):
    nonlocal import_var
    if (
        _context.caller_package_id.repository_id != PROTO_REPO
        and d.repository_id == PROTO_REPO
    ):
      import_var = "${Protobuf_IMPORT_DIRS}"

  # Validate src properties: files ending in .proto within the same repo,
  # and add them to the proto_src_files.
  srcs_collector = state.collect_targets(resolved_srcs)
  proto_src_files = sorted(set(srcs_collector.file_paths()))

  for proto in resolved_srcs:
    assert proto.target_name.endswith(".proto"), f"{proto} must end in .proto"
    # Verify that the source is in the same repository as the proto_library rule
    assert proto.repository_id == _target.repository_id
    maybe_set_import_var(proto)

  # Resolve deps. When using system protobuffers, well-known-proto targets need
  # 'Protobuf_IMPORT_DIRS' added to their transitive includes.
  for t in resolved_deps:
    maybe_set_import_var(t)
  deps_collector = state.collect_deps(resolved_deps, alias=False)

  # In order to propagate the includes to our compile targets, each
  # proto_library() becomes a custom CMake target which contains an
  # INTERFACE_INCLUDE_DIRECTORIES property which can be used by the protoc
  # compiler.
  if import_prefix and strip_import_prefix:
    print(
        f"Warning: package {_context.caller_package_id.package_name} has both"
        f" strip_import_prefix={strip_import_prefix} and"
        f" import_prefix={import_prefix}."
    )

  repo = state.workspace.all_repositories.get(_target.repository_id)
  assert repo is not None

  # strip_import_prefix and import_prefix behave the same as for cc_library
  target_includes = construct_cc_includes(
      _context.caller_package_id,
      source_directory=repo.source_directory,
      cmake_binary_dir=repo.cmake_binary_dir,
      includes=None,
      include_prefix=import_prefix,
      strip_include_prefix=strip_import_prefix,
      known_include_files=proto_src_files,
  )
  includes = set(repo.replace_with_cmake_macro_dirs(
      target_includes.public | target_includes.system
  ))

  # Sanity check; if there are sources, then there should be includes.
  if proto_src_files:
    assert includes

  out = io.StringIO()
  out.write(f"\n# proto_library({_target.as_label()})\n")
  emit_filegroup(
      out,
      cmake_name=cmake_target_pair.target,
      filegroup_files=repo.replace_with_cmake_macro_dirs(proto_src_files),
      source_directory=repo.source_directory,
      cmake_binary_dir=repo.cmake_binary_dir,
      add_dependencies=list(srcs_collector.add_dependencies()),
      link_libraries=deps_collector.link_libraries(),
      includes=includes,
  )
  if cmake_target_pair.alias is not None:
    out.write(
        f"add_library({cmake_target_pair.alias} ALIAS"
        f" {cmake_target_pair.target})\n"
    )

  _context.access(CMakeBuilder).addtext(out.getvalue())
  _context.add_analyzed_target(
      _target,
      TargetInfo(
          *default_providers(cmake_target_pair),
          FilesProvider(proto_src_files),
          ProtoLibraryProvider(
              _target,
              resolved_srcs,
              resolved_deps,
              strip_import_prefix,
              import_prefix,
          ),
      ),
  )


@register_native_build_rule
def proto_lang_toolchain(
    self: InvocationContext,
    name: str,
    command_line: str,
    runtime: str,
    visibility: Optional[List[RelativeLabel]] = None,
    **kwargs,
):
  pass
