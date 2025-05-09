load("//bazel:tensorstore.bzl", "tensorstore_cc_library", "tensorstore_cc_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

filegroup(
    name = "doc_sources",
    srcs = glob([
        "**/*.rst",
        "**/*.yml",
    ]),
)

tensorstore_cc_library(
    name = "kvstack",
    srcs = ["kvstack_key_value_store.cc"],
    deps = [
        ":key_range_map",
        "//tensorstore:context",
        "//tensorstore:transaction",
        "//tensorstore/internal:context_binding",
        "//tensorstore/internal:data_copy_concurrency_resource",
        "//tensorstore/internal:intrusive_ptr",
        "//tensorstore/internal/cache_key",
        "//tensorstore/internal/json",
        "//tensorstore/internal/json_binding",
        "//tensorstore/internal/json_binding:bindable",
        "//tensorstore/kvstore",
        "//tensorstore/kvstore:generation",
        "//tensorstore/kvstore:key_range",
        "//tensorstore/kvstore/gcs:gcs_resource",
        "//tensorstore/kvstore/gcs_http",
        "//tensorstore/kvstore/gcs_http:gcs_resource",
        "//tensorstore/serialization",
        "//tensorstore/util:executor",
        "//tensorstore/util:future",
        "//tensorstore/util:quote_string",
        "//tensorstore/util:result",
        "//tensorstore/util:span",
        "//tensorstore/util:str_cat",
        "//tensorstore/util/apply_members",
        "//tensorstore/util/execution",
        "//tensorstore/util/execution:any_receiver",
        "//tensorstore/util/garbage_collection",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
    alwayslink = True,
)

tensorstore_cc_test(
    name = "kvstack_test",
    srcs = ["kvstack_test.cc"],
    deps = [
        ":kvstack",  # build_cleaner: keep
        "//tensorstore:context",
        "//tensorstore:transaction",
        "//tensorstore/internal:global_initializer",
        "//tensorstore/internal/cache:kvs_backed_cache_testutil",
        "//tensorstore/kvstore",
        "//tensorstore/kvstore:key_range",
        "//tensorstore/kvstore:mock_kvstore",
        "//tensorstore/kvstore:test_matchers",
        "//tensorstore/kvstore:test_util",
        "//tensorstore/kvstore/memory",
        "//tensorstore/util:future",
        "//tensorstore/util:result",
        "//tensorstore/util:status_testutil",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest_main",
        "@nlohmann_json//:json",
    ],
)

tensorstore_cc_library(
    name = "key_range_map",
    hdrs = ["key_range_map.h"],
    deps = [
        "//tensorstore/kvstore:key_range",
        "@abseil-cpp//absl/container:btree",
    ],
)

tensorstore_cc_test(
    name = "key_range_map_test",
    srcs = ["key_range_map_test.cc"],
    deps = [
        ":key_range_map",
        "//tensorstore/kvstore:key_range",
        "@abseil-cpp//absl/log:absl_log",
        "@googletest//:gtest_main",
    ],
)
