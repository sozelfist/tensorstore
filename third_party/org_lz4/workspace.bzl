# Copyright 2020 The TensorStore Authors
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

# buildifier: disable=module-docstring

load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("//third_party:repo.bzl", "third_party_http_archive")

def repo():
    maybe(
        third_party_http_archive,
        name = "org_lz4",
        strip_prefix = "lz4-1.10.0",
        urls = [
            "https://storage.googleapis.com/tensorstore-bazel-mirror/github.com/lz4/lz4/archive/v1.10.0.zip",
        ],
        sha256 = "3224b4c80f351f194984526ef396f6079bd6332dd9825c72ac0d7a37b3cdc565",
        build_file = Label("//third_party:org_lz4/lz4.BUILD.bazel"),
        system_build_file = Label("//third_party:org_lz4/system.BUILD.bazel"),
        cmake_name = "LZ4",
        cmake_target_mapping = {
            "@org_lz4//:lz4": "LZ4::LZ4",
        },
        bazel_to_cmake = {},
        cmake_package_redirect_libraries = {
            "LZ4": "LZ4::LZ4",
        },
    )
