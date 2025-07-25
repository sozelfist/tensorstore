load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("//bazel:tensorstore.bzl", "tensorstore_cc_library", "tensorstore_cc_test")
load("//docs:doctest.bzl", "doctest_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

DRIVERS = [
    "array",
    "auto",
    "cast",
    "downsample",
    "image/avif",
    "image/jpeg",
    "image/png",
    "image/tiff",
    "image/webp",
    "image/bmp",
    "json",
    "n5",
    "neuroglancer_precomputed",
    "stack",
    "virtual_chunked",
    "zarr",
    "zarr3",
]

DOCTEST_SOURCES = glob([
    "**/*.rst",
    "**/*.yml",
])

doctest_test(
    name = "doctest_test",
    srcs = DOCTEST_SOURCES,
)

filegroup(
    name = "doc_sources",
    srcs = DOCTEST_SOURCES + [
        "//tensorstore/driver/" + driver + ":doc_sources"
        for driver in DRIVERS
    ],
)

EXTRA_DRIVERS = []

tensorstore_cc_library(
    name = "all_drivers",
    deps = [
        "//tensorstore/driver/" + driver
        for driver in DRIVERS
    ] + EXTRA_DRIVERS,
    alwayslink = True,
)

tensorstore_cc_library(
    name = "chunk",
    hdrs = ["chunk.h"],
    deps = [
        "//tensorstore:read_write_options",
        "//tensorstore/index_space:index_transform",
        "//tensorstore/index_space:transformed_array",
        "//tensorstore/internal:arena",
        "//tensorstore/internal:lock_collection",
        "//tensorstore/internal:nditerable",
        "//tensorstore/internal/poly",
        "//tensorstore/util:future",
        "//tensorstore/util:result",
        "//tensorstore/util/execution:any_receiver",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
    ],
)

tensorstore_cc_library(
    name = "chunk_cache_driver",
    hdrs = ["chunk_cache_driver.h"],
    deps = [
        ":chunk",
        ":driver",
        "//tensorstore:data_type",
        "//tensorstore:index",
        "//tensorstore:staleness_bound",
        "//tensorstore/internal:chunk_grid_specification",
        "//tensorstore/internal/cache",
        "//tensorstore/internal/cache:chunk_cache",
        "//tensorstore/util:executor",
    ],
)

tensorstore_cc_library(
    name = "driver",
    srcs = [
        "copy.cc",
        "driver.cc",
        "driver_spec.cc",
        "read.cc",
        "url_registry.cc",
        "write.cc",
    ],
    hdrs = [
        "copy.h",
        "driver.h",
        "driver_handle.h",
        "driver_spec.h",
        "read.h",
        "registry.h",
        "url_registry.h",
        "write.h",
    ],
    deps = [
        ":chunk",
        ":read_request",
        ":write_request",
        "//tensorstore:array",
        "//tensorstore:array_storage_statistics",
        "//tensorstore:batch",
        "//tensorstore:box",
        "//tensorstore:chunk_layout",
        "//tensorstore:codec_spec",
        "//tensorstore:container_kind",
        "//tensorstore:context",
        "//tensorstore:contiguous_layout",
        "//tensorstore:data_type",
        "//tensorstore:index",
        "//tensorstore:json_serialization_options",
        "//tensorstore:open_mode",
        "//tensorstore:open_options",
        "//tensorstore:progress",
        "//tensorstore:rank",
        "//tensorstore:read_write_options",
        "//tensorstore:resize_options",
        "//tensorstore:schema",
        "//tensorstore:transaction",
        "//tensorstore/index_space:alignment",
        "//tensorstore/index_space:dimension_units",
        "//tensorstore/index_space:index_transform",
        "//tensorstore/index_space:transform_broadcastable_array",
        "//tensorstore/index_space:transformed_array",
        "//tensorstore/internal:context_binding",
        "//tensorstore/internal:driver_kind_registry",
        "//tensorstore/internal:intrusive_ptr",
        "//tensorstore/internal:json_registry",
        "//tensorstore/internal:lock_collection",
        "//tensorstore/internal:nditerable",
        "//tensorstore/internal:nditerable_copy",
        "//tensorstore/internal:nditerable_data_type_conversion",
        "//tensorstore/internal:nditerable_transformed_array",
        "//tensorstore/internal:nditerable_util",
        "//tensorstore/internal:tagged_ptr",
        "//tensorstore/internal/json_binding",
        "//tensorstore/internal/json_binding:bindable",
        "//tensorstore/internal/json_binding:data_type",
        "//tensorstore/internal/meta:type_traits",
        "//tensorstore/internal/tracing",
        "//tensorstore/kvstore",
        "//tensorstore/serialization",
        "//tensorstore/serialization:registry",
        "//tensorstore/util:element_pointer",
        "//tensorstore/util:executor",
        "//tensorstore/util:extents",
        "//tensorstore/util:future",
        "//tensorstore/util:quote_string",
        "//tensorstore/util:result",
        "//tensorstore/util:span",
        "//tensorstore/util:status",
        "//tensorstore/util:str_cat",
        "//tensorstore/util/execution",
        "//tensorstore/util/execution:any_receiver",
        "//tensorstore/util/execution:sender",
        "//tensorstore/util/execution:sender_util",
        "//tensorstore/util/garbage_collection",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/synchronization",
        "@nlohmann_json//:json",
    ],
)

tensorstore_cc_test(
    name = "driver_test",
    size = "small",
    srcs = ["driver_test.cc"],
    deps = [
        ":driver",
        "//tensorstore:array",
        "//tensorstore:data_type",
        "//tensorstore:index",
        "//tensorstore:open_mode",
        "//tensorstore:progress",
        "//tensorstore/index_space:index_transform",
        "//tensorstore/util:executor",
        "//tensorstore/util:status_testutil",
        "//tensorstore/util/execution",
        "//tensorstore/util/execution:any_receiver",
        "//tensorstore/util/garbage_collection",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

tensorstore_cc_library(
    name = "driver_testutil",
    testonly = 1,
    srcs = ["driver_testutil.cc"],
    hdrs = ["driver_testutil.h"],
    deps = [
        ":chunk",
        ":driver",
        "//tensorstore",
        "//tensorstore:array",
        "//tensorstore:array_testutil",
        "//tensorstore:auto",
        "//tensorstore:box",
        "//tensorstore:chunk_layout",
        "//tensorstore:context",
        "//tensorstore:contiguous_layout",
        "//tensorstore:data_type",
        "//tensorstore:index",
        "//tensorstore:index_interval",
        "//tensorstore:json_serialization_options",
        "//tensorstore:json_serialization_options_base",
        "//tensorstore:open",
        "//tensorstore:open_mode",
        "//tensorstore:open_options",
        "//tensorstore:rank",
        "//tensorstore:resize_options",
        "//tensorstore:schema",
        "//tensorstore:spec",
        "//tensorstore:strided_layout",
        "//tensorstore:transaction",
        "//tensorstore/index_space:dim_expression",
        "//tensorstore/index_space:index_transform",
        "//tensorstore/index_space:index_transform_testutil",
        "//tensorstore/index_space:transformed_array",
        "//tensorstore/internal:arena",
        "//tensorstore/internal:data_type_random_generator",
        "//tensorstore/internal:lock_collection",
        "//tensorstore/internal:nditerable",
        "//tensorstore/internal:nditerable_transformed_array",
        "//tensorstore/internal:uri_utils",
        "//tensorstore/internal/testing:dynamic",
        "//tensorstore/internal/testing:json_gtest",
        "//tensorstore/internal/testing:queue_testutil",
        "//tensorstore/internal/testing:random_seed",
        "//tensorstore/internal/testing:scoped_directory",
        "//tensorstore/kvstore:generation",
        "//tensorstore/kvstore:mock_kvstore",
        "//tensorstore/kvstore:test_util",
        "//tensorstore/kvstore/memory",
        "//tensorstore/serialization:test_util",
        "//tensorstore/util:constant_vector",
        "//tensorstore/util:element_pointer",
        "//tensorstore/util:executor",
        "//tensorstore/util:extents",
        "//tensorstore/util:future",
        "//tensorstore/util:iterate",
        "//tensorstore/util:iterate_over_index_range",
        "//tensorstore/util:result",
        "//tensorstore/util:span",
        "//tensorstore/util:status",
        "//tensorstore/util:status_testutil",
        "//tensorstore/util:str_cat",
        "//tensorstore/util/execution:any_receiver",
        "//tensorstore/util/execution:sync_flow_sender",
        "//tensorstore/util/garbage_collection",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:bit_gen_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest",
        "@nlohmann_json//:json",
    ],
)

bool_flag(
    name = "kvs_backed_chunk_driver_debug",
    build_setting_default = False,
)

config_setting(
    name = "kvs_backed_chunk_driver_debug_setting",
    flag_values = {
        ":kvs_backed_chunk_driver_debug": "True",
    },
    visibility = ["//visibility:private"],
)

# To enable debug logging, specify:
# bazel build --//tensorstore/driver:kvs_backed_chunk_driver_debug
tensorstore_cc_library(
    name = "kvs_backed_chunk_driver",
    srcs = [
        "kvs_backed_chunk_driver.cc",
    ],
    hdrs = [
        "kvs_backed_chunk_driver.h",
        "kvs_backed_chunk_driver_impl.h",
    ],
    local_defines = select({
        ":kvs_backed_chunk_driver_debug_setting": ["TENSORSTORE_KVS_DRIVER_DEBUG"],
        "//conditions:default": [],
    }),
    deps = [
        ":chunk_cache_driver",
        ":driver",
        "//tensorstore:batch",
        "//tensorstore:box",
        "//tensorstore:chunk_layout",
        "//tensorstore:context",
        "//tensorstore:index",
        "//tensorstore:index_interval",
        "//tensorstore:json_serialization_options",
        "//tensorstore:open_mode",
        "//tensorstore:open_options",
        "//tensorstore:rank",
        "//tensorstore:resize_options",
        "//tensorstore:schema",
        "//tensorstore:staleness_bound",
        "//tensorstore:transaction",
        "//tensorstore/index_space:index_transform",
        "//tensorstore/internal:box_difference",
        "//tensorstore/internal:chunk_grid_specification",
        "//tensorstore/internal:context_binding",
        "//tensorstore/internal:data_copy_concurrency_resource",
        "//tensorstore/internal:intrusive_ptr",
        "//tensorstore/internal:mutex",
        "//tensorstore/internal:open_mode_spec",
        "//tensorstore/internal:path",
        "//tensorstore/internal:unowned_to_shared",
        "//tensorstore/internal:uri_utils",
        "//tensorstore/internal/cache",
        "//tensorstore/internal/cache:aggregate_writeback_cache",
        "//tensorstore/internal/cache:async_cache",
        "//tensorstore/internal/cache:async_initialized_cache_mixin",
        "//tensorstore/internal/cache:cache_pool_resource",
        "//tensorstore/internal/cache:chunk_cache",
        "//tensorstore/internal/cache:kvs_backed_cache",
        "//tensorstore/internal/cache:kvs_backed_chunk_cache",
        "//tensorstore/internal/cache_key",
        "//tensorstore/internal/estimate_heap_usage",
        "//tensorstore/internal/json_binding",
        "//tensorstore/internal/json_binding:bindable",
        "//tensorstore/internal/json_binding:staleness_bound",
        "//tensorstore/internal/meta:type_traits",
        "//tensorstore/kvstore",
        "//tensorstore/kvstore:generation",
        "//tensorstore/kvstore:key_range",
        "//tensorstore/serialization",
        "//tensorstore/serialization:absl_time",
        "//tensorstore/util:dimension_set",
        "//tensorstore/util:executor",
        "//tensorstore/util:future",
        "//tensorstore/util:iterate",
        "//tensorstore/util:iterate_over_index_range",
        "//tensorstore/util:result",
        "//tensorstore/util:span",
        "//tensorstore/util:status",
        "//tensorstore/util:str_cat",
        "//tensorstore/util/execution",
        "//tensorstore/util/garbage_collection",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/time",
        "@nlohmann_json//:json",
    ],
)

tensorstore_cc_test(
    name = "kvs_backed_chunk_driver_test",
    size = "small",
    srcs = ["kvs_backed_chunk_driver_test.cc"],
    deps = [
        ":kvs_backed_chunk_driver",
        "//tensorstore:box",
        "//tensorstore:index",
        "//tensorstore/util:span",
        "//tensorstore/util:status",
        "//tensorstore/util:status_testutil",
        "@googletest//:gtest_main",
    ],
)

tensorstore_cc_library(
    name = "chunk_receiver_utils",
    hdrs = ["chunk_receiver_utils.h"],
    deps = [
        ":chunk",
        "//tensorstore/index_space:index_transform",
        "//tensorstore/internal:intrusive_ptr",
        "//tensorstore/util:future",
        "//tensorstore/util/execution:any_receiver",
        "//tensorstore/util/execution:flow_sender_operation_state",
        "@abseil-cpp//absl/status",
    ],
)

tensorstore_cc_library(
    name = "read_request",
    hdrs = ["read_request.h"],
    deps = [
        "//tensorstore:batch",
        "//tensorstore:transaction",
        "//tensorstore/index_space:index_transform",
    ],
)

tensorstore_cc_library(
    name = "write_request",
    hdrs = ["write_request.h"],
    deps = [
        "//tensorstore:transaction",
        "//tensorstore/index_space:index_transform",
    ],
)

tensorstore_cc_test(
    name = "all_drivers_test",
    srcs = ["all_drivers_test.cc"],
    deps = [
        ":all_drivers",  # buildcleaner: keep
        "//tensorstore:open",
        "//tensorstore:open_mode",
        "//tensorstore/kvstore/memory",  # buildcleaner: keep
        "//tensorstore/util:status_testutil",
        "@googletest//:gtest_main",
        "@nlohmann_json//:json",
    ],
)
