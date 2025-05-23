load("//bazel:tensorstore.bzl", "tensorstore_cc_library", "tensorstore_cc_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

tensorstore_cc_library(
    name = "admission_queue",
    srcs = ["admission_queue.cc"],
    hdrs = ["admission_queue.h"],
    deps = [
        ":rate_limiter",
        "//tensorstore/internal/container:intrusive_linked_list",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
    ],
)

tensorstore_cc_test(
    name = "admission_queue_test",
    srcs = ["admission_queue_test.cc"],
    deps = [
        ":admission_queue",
        ":rate_limiter",
        "//tensorstore/internal:intrusive_ptr",
        "//tensorstore/util:executor",
        "@googletest//:gtest_main",
    ],
)

tensorstore_cc_library(
    name = "scaling_rate_limiter",
    srcs = ["scaling_rate_limiter.cc"],
    hdrs = ["scaling_rate_limiter.h"],
    deps = [
        ":token_bucket_rate_limiter",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/time",
    ],
)

tensorstore_cc_test(
    name = "scaling_rate_limiter_test",
    srcs = ["scaling_rate_limiter_test.cc"],
    deps = [
        ":rate_limiter",
        ":scaling_rate_limiter",
        "//tensorstore/internal:intrusive_ptr",
        "//tensorstore/util:executor",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

tensorstore_cc_library(
    name = "rate_limiter",
    srcs = ["rate_limiter.cc"],
    hdrs = ["rate_limiter.h"],
    deps = ["//tensorstore/internal/container:intrusive_linked_list"],
)

tensorstore_cc_test(
    name = "rate_limiter_test",
    srcs = ["rate_limiter_test.cc"],
    deps = [
        ":rate_limiter",
        "//tensorstore/internal:intrusive_ptr",
        "@googletest//:gtest_main",
    ],
)

tensorstore_cc_library(
    name = "token_bucket_rate_limiter",
    srcs = ["token_bucket_rate_limiter.cc"],
    hdrs = ["token_bucket_rate_limiter.h"],
    deps = [
        ":rate_limiter",
        "//tensorstore/internal/container:intrusive_linked_list",
        "//tensorstore/internal/log:verbose_flag",
        "//tensorstore/internal/thread:schedule_at",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)
