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

"""Supports definining bazel repos for third-party dependencies.

The `TENSORSTORE_SYSTEM_LIBS` environment variable may be used to specify that
system libraries should be used in place of bundled libraries. It should be set
to a comma-separated list of the repo names, e.g.
`TENSORSTORE_SYSTEM_LIBS=zlib,curl` to use system-provided zlib and libcurl.
"""

load("//bazel/repo_rules:third_party_http_archive.bzl", _http_archive = "third_party_http_archive")
load("//bazel/repo_rules:third_party_python_package.bzl", _python_package = "third_party_python_package")

third_party_http_archive = _http_archive
third_party_python_package = _python_package
