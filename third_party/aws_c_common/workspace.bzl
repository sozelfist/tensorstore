# Copyright 2024 The TensorStore Authors
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
        name = "aws_c_common",
        sha256 = "ecea168ea974f2da73b5a0adc19d9c5ebca73ca4b9f733de7c37fc453ee7d1c2",
        strip_prefix = "aws-c-common-0.12.2",
        urls = [
            "https://storage.googleapis.com/tensorstore-bazel-mirror/github.com/awslabs/aws-c-common/archive/v0.12.2.tar.gz",
        ],
        build_file = Label("//third_party:aws_c_common/aws_c_common.BUILD.bazel"),
        cmake_name = "aws_c_common",
        cmake_target_mapping = {
            "@aws_c_common//:aws_c_common": "aws_c_common::aws_c_common",
        },
        bazel_to_cmake = {},
    )
