// Copyright 2020 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tensorstore/spec.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/status/status.h"
#include <nlohmann/json_fwd.hpp>
#include "tensorstore/box.h"
#include "tensorstore/context.h"
#include "tensorstore/index.h"
#include "tensorstore/index_space/dim_expression.h"
#include "tensorstore/index_space/index_transform.h"
#include "tensorstore/internal/json_binding/bindable.h"
#include "tensorstore/internal/json_binding/gtest.h"
#include "tensorstore/internal/testing/json_gtest.h"
#include "tensorstore/json_serialization_options.h"
#include "tensorstore/json_serialization_options_base.h"
#include "tensorstore/kvstore/spec.h"
#include "tensorstore/serialization/serialization.h"
#include "tensorstore/serialization/test_util.h"
#include "tensorstore/util/result.h"
#include "tensorstore/util/status_testutil.h"
#include "tensorstore/util/str_cat.h"

namespace {

using ::nlohmann::json;
using ::tensorstore::DimensionIndex;
using ::tensorstore::IsOkAndHolds;
using ::tensorstore::MatchesJson;
using ::tensorstore::MatchesStatus;
using ::tensorstore::Spec;
using ::tensorstore::serialization::SerializationRoundTrip;
using ::tensorstore::serialization::TestSerializationRoundTrip;

TEST(SpecTest, Invalid) {
  Spec spec;
  EXPECT_FALSE(spec.valid());
}

TEST(SpecTest, ToJson) {
  ::nlohmann::json spec_json{
      {"driver", "array"},
      {"dtype", "int32"},
      {"array", {1, 2, 3}},
      {"transform",
       {{"input_inclusive_min", {0}}, {"input_exclusive_max", {3}}}},
  };
  Spec spec = Spec::FromJson(spec_json).value();
  EXPECT_THAT(spec.ToJson(), ::testing::Optional(MatchesJson(spec_json)));
  EXPECT_TRUE(spec.valid());
}

TEST(SpecTest, Comparison) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(  //
      auto spec_a, Spec::FromJson({
                       {"driver", "array"},
                       {"dtype", "int32"},
                       {"array", {1, 2, 3}},
                       {"rank", 1},
                   }));
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(  //
      auto spec_b, Spec::FromJson({
                       {"driver", "array"},
                       {"dtype", "int32"},
                       {"array", {1, 2, 3, 4}},
                       {"rank", 1},
                   }));
  EXPECT_EQ(spec_a, spec_a);
  EXPECT_NE(spec_a, spec_b);

  // Bind `spec_a` and `spec_b` to contexts.  This binds the
  // "data_copy_concurrency" resource (which is not explicitly specified by the
  // above JSON specifications).
  auto context1 = tensorstore::Context::Default();
  auto context2 = tensorstore::Context::Default();

  auto a1x = spec_a;
  auto a1y = spec_a;
  TENSORSTORE_ASSERT_OK(a1x.BindContext(context1));
  TENSORSTORE_ASSERT_OK(a1y.BindContext(context1));

  // a1x and a1y do not refer to identical `Driver::Spec` objects, but the spec
  // has the same JSON representation and they are bound to the same resources.
  EXPECT_EQ(a1x, a1y);

  // a2 and a1x are equivalent specs but not bound to the same resources.
  auto a2 = spec_a;
  TENSORSTORE_ASSERT_OK(a2.BindContext(context2));
  EXPECT_NE(a1x, a2);

  auto a2_unbound = a2;
  a2_unbound.UnbindContext();

  auto a1_unbound = a1x;
  a1_unbound.UnbindContext();

  // The unbound representations (which include the "data_copy_concurrency"
  // resource explicitly) are identical.
  EXPECT_EQ(a1_unbound, a2_unbound);

  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto a3, Spec::FromJson({
                   {"driver", "array"},
                   {"dtype", "int32"},
                   {"array", {1, 2, 3}},
                   {"rank", 1},
                   {"context",
                    {{"data_copy_concurrency", ::nlohmann::json::object_t()}}},
               }));
  EXPECT_EQ(a3, a1_unbound);
}

TEST(SpecTest, ApplyIndexTransform) {
  // Tests successfully applying a DimExpression to a Spec.
  auto spec_with_transform =
      Spec::FromJson(
          ::nlohmann::json({{"driver", "array"},
                            {"dtype", "int32"},
                            {"array",
                             {
                                 {1, 2, 3, 4},
                                 {5, 6, 7, 8},
                                 {9, 10, 11, 12},
                             }},
                            {"transform",
                             {{"input_inclusive_min", {2, 4}},
                              {"input_shape", {3, 2}},
                              {"output",
                               {{{"input_dimension", 0}, {"offset", -2}},
                                {{"input_dimension", 1}, {"offset", -4}}}}}}}))
          .value();
  EXPECT_EQ(2, spec_with_transform.rank());
  EXPECT_THAT(Spec::FromJson(::nlohmann::json(
                  {{"driver", "array"},
                   {"dtype", "int32"},
                   {"array",
                    {
                        {1, 2, 3, 4},
                        {5, 6, 7, 8},
                        {9, 10, 11, 12},
                    }},
                   {"transform",
                    {{"input_inclusive_min", {2, 4}},
                     {"input_shape", {3, 4}},
                     {"output",
                      {{{"input_dimension", 0}, {"offset", -2}},
                       {{"input_dimension", 1}, {"offset", -4}}}}}}})) |
                  tensorstore::Dims(1).SizedInterval(4, 2),
              ::testing::Optional(spec_with_transform));

  // Tests applying a DimExpression to a Spec with unknown rank (fails).
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(auto spec_without_transform,
                                   Spec::FromJson({
                                       {"driver", "zarr"},
                                       {"kvstore", {{"driver", "memory"}}},
                                   }));
  EXPECT_THAT(
      spec_without_transform | tensorstore::Dims(1).SizedInterval(1, 2),
      MatchesStatus(
          absl::StatusCode::kInvalidArgument,
          "Cannot perform indexing operations on Spec with unspecified rank"));
}

TEST(SpecTest, ApplyBox) {
  // Tests successfully applying a Box to a Spec.
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto spec_with_transform,
      Spec::FromJson(::nlohmann::json(
          {{"driver", "array"},
           {"dtype", "int32"},
           {"array",
            {
                {1, 2, 3, 4},
                {5, 6, 7, 8},
                {9, 10, 11, 12},
            }},
           {"transform",
            {{"input_inclusive_min", {3, 4}},
             {"input_shape", {2, 2}},
             {"output",
              {{{"input_dimension", 0}, {"offset", -2}},
               {{"input_dimension", 1}, {"offset", -4}}}}}}})));
  EXPECT_EQ(2, spec_with_transform.rank());
  EXPECT_THAT(Spec::FromJson(::nlohmann::json(
                  {{"driver", "array"},
                   {"dtype", "int32"},
                   {"array",
                    {
                        {1, 2, 3, 4},
                        {5, 6, 7, 8},
                        {9, 10, 11, 12},
                    }},
                   {"transform",
                    {{"input_inclusive_min", {2, 4}},
                     {"input_shape", {3, 4}},
                     {"output",
                      {{{"input_dimension", 0}, {"offset", -2}},
                       {{"input_dimension", 1}, {"offset", -4}}}}}}})) |
                  tensorstore::Box({3, 4}, {2, 2}),
              ::testing::Optional(spec_with_transform));
}

TEST(SpecTest, PrintToOstream) {
  ::nlohmann::json spec_json{
      {"driver", "array"},
      {"dtype", "int32"},
      {"array", {1, 2, 3}},
      {"transform",
       {{"input_inclusive_min", {0}}, {"input_exclusive_max", {3}}}},
  };
  Spec spec = Spec::FromJson(spec_json).value();
  EXPECT_EQ(spec_json.dump(), tensorstore::StrCat(spec));
}

TEST(SpecTest, UnknownRankApplyIndexTransform) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto spec, Spec::FromJson({{"driver", "zarr"},
                                 {"kvstore", {{"driver", "memory"}}}}));
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(spec,
                                   spec | tensorstore::IdentityTransform(3));
  EXPECT_EQ(spec.transform(), tensorstore::IdentityTransform(3));
}

TEST(SpecTest, Schema) {
  tensorstore::TestJsonBinderRoundTripJsonOnly<Spec>(
      {
          {{"driver", "array"},
           {"dtype", "int32"},
           {"array", {1, 2, 3}},
           {"transform",
            {{"input_inclusive_min", {0}}, {"input_exclusive_max", {3}}}},
           {"schema", {{"fill_value", 42}}}},
      },
      tensorstore::internal_json_binding::DefaultBinder<>,
      tensorstore::IncludeDefaults{false});
}

TEST(SpecTest, PreserveBoundContextResources) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(auto spec, Spec::FromJson({
                                                  {"driver", "array"},
                                                  {"array", {1, 2, 3}},
                                                  {"dtype", "uint8"},
                                              }));
  TENSORSTORE_ASSERT_OK(spec.Set(tensorstore::Context::Default()));
  tensorstore::JsonSerializationOptions json_serialization_options;
  json_serialization_options.preserve_bound_context_resources_ = true;
  EXPECT_THAT(
      spec.ToJson(json_serialization_options),
      ::testing::Optional(MatchesJson({
          {"driver", "array"},
          {"array", {1, 2, 3}},
          {"dtype", "uint8"},
          {"transform",
           {{"input_inclusive_min", {0}}, {"input_exclusive_max", {3}}}},
          {"data_copy_concurrency", {"data_copy_concurrency"}},
          {"context",
           {{"data_copy_concurrency", ::nlohmann::json::object_t()}}},
      })));
}

// Tests that when `Spec::Set` is called with both a `kvstore::Spec` and a
// `Context`, the context is also bound to the kvstore spec.
TEST(SpecTest, SetContextAndKvstoreIncludeDefaults) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto spec, Spec::FromJson({
                     {"driver", "zarr"},
                     {"schema", {{"domain", {{"shape", {10}}}}}},
                     {"dtype", "uint8"},
                 }));
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto kvstore_spec,
      tensorstore::kvstore::Spec::FromJson(
          {{"driver", "file"},
           {"path", "/tmp/"},
           {"file_io_concurrency", "file_io_concurrency#a"}}));
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto context, tensorstore::Context::FromJson(
                        {{"file_io_concurrency#a", {{"limit", 5}}}}));
  TENSORSTORE_EXPECT_OK(spec.Set(context, kvstore_spec));
  tensorstore::JsonSerializationOptions json_serialization_options;
  json_serialization_options.Set(tensorstore::IncludeDefaults{true});
  json_serialization_options.preserve_bound_context_resources_ = true;
  EXPECT_THAT(
      spec.ToJson(json_serialization_options),
      ::testing::Optional(MatchesJson({
          {"driver", "zarr"},
          {"dtype", "uint8"},
          {"assume_cached_metadata", false},
          {"assume_metadata", false},
          {"cache_pool", {"cache_pool"}},
          {"data_copy_concurrency", {"data_copy_concurrency"}},
          {"delete_existing", false},
          {"metadata", ::nlohmann::json::object_t()},
          {"recheck_cached_data", true},
          {"recheck_cached_metadata", "open"},
          {"fill_missing_data_reads", true},
          {"store_data_equal_to_fill_value", false},
          {"kvstore",
           {
               {"context", ::nlohmann::json::object_t()},
               {"driver", "file"},
               {"path", "/tmp/"},
               {"file_io_concurrency", {"file_io_concurrency#a"}},
               {"file_io_sync", {"file_io_sync"}},
               {"file_io_locking", {"file_io_locking"}},
               {"file_io_mode", {"file_io_mode"}},
           }},
          {"schema",
           {{"dtype", "uint8"},
            {"rank", 1},
            {"domain", {{"inclusive_min", {0}}, {"exclusive_max", {10}}}}}},
          {"transform",
           {{"input_inclusive_min", {0}}, {"input_exclusive_max", {{10}}}}},
          {"context",
           {
               {"data_copy_concurrency", {{"limit", "shared"}}},
               {"cache_pool", {{"total_bytes_limit", 0}}},
               {"file_io_concurrency#a", {{"limit", 5}}},
               {"file_io_locking", ::nlohmann::json::object_t()},
               {"file_io_sync", true},
               {"file_io_mode", ::nlohmann::json::object_t()},
           }},
      })));
}

TEST(SpecTest, SetContextAndKvstore) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto spec, Spec::FromJson({
                     {"driver", "zarr"},
                     {"schema", {{"domain", {{"shape", {10}}}}}},
                     {"dtype", "uint8"},
                 }));
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto kvstore_spec,
      tensorstore::kvstore::Spec::FromJson(
          {{"driver", "file"},
           {"path", "/tmp/"},
           {"file_io_concurrency", "file_io_concurrency#a"}}));
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto context, tensorstore::Context::FromJson(
                        {{"file_io_concurrency#a", {{"limit", 5}}}}));
  TENSORSTORE_EXPECT_OK(spec.Set(context, kvstore_spec));
  tensorstore::JsonSerializationOptions json_serialization_options;
  json_serialization_options.preserve_bound_context_resources_ = true;
  EXPECT_THAT(
      spec.ToJson(json_serialization_options),
      ::testing::Optional(MatchesJson({
          {"driver", "zarr"},
          {"kvstore",
           {
               {"driver", "file"},
               {"path", "/tmp/"},
               {"file_io_concurrency", {"file_io_concurrency#a"}},
               {"file_io_sync", {"file_io_sync"}},
               {"file_io_locking", {"file_io_locking"}},
               {"file_io_mode", {"file_io_mode"}},
           }},
          {"dtype", "uint8"},
          {"cache_pool", {"cache_pool"}},
          {"schema",
           {{"domain", {{"inclusive_min", {0}}, {"exclusive_max", {10}}}}}},
          {"transform",
           {{"input_inclusive_min", {0}}, {"input_exclusive_max", {{10}}}}},
          {"data_copy_concurrency", {"data_copy_concurrency"}},
          {"context",
           {
               {"data_copy_concurrency", ::nlohmann::json::object_t()},
               {"cache_pool", ::nlohmann::json::object_t()},
               {"file_io_concurrency#a", {{"limit", 5}}},
               {"file_io_locking", ::nlohmann::json::object_t()},
               {"file_io_sync", true},
               {"file_io_mode", ::nlohmann::json::object_t()},
           }},
      })));
}

TEST(SpecTest, FromUrlErrors) {
  EXPECT_THAT(Spec::FromUrl(""),
              MatchesStatus(absl::StatusCode::kInvalidArgument,
                            ".*URL must be non-empty.*"));

  EXPECT_THAT(Spec::FromUrl("memory://|"),
              MatchesStatus(absl::StatusCode::kInvalidArgument,
                            ".*unsupported URL scheme.*"));
}

TEST(SpecTest, FromUrl) {
  EXPECT_THAT(Spec::FromUrl("file:///C:/tmp/|zarr"),
              MatchesStatus(absl::StatusCode::kInvalidArgument,
                            ".*unsupported URL scheme: .zarr.*"));
  // This relies on zarr2 being linked in.  If it is not linked in, then the
  // driver will not be found and the call will fail.
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(auto spec,
                                   Spec::FromUrl("file:///C:/tmp/|zarr2"));

  EXPECT_THAT(spec.ToJson(),  //
              ::testing::Optional(MatchesJson({
                  {"driver", "zarr"},
                  {"kvstore",
                   {
                       {"driver", "file"},
                       {"path", "C:/tmp/"},
                   }},
              })));

  EXPECT_THAT(spec.ToUrl(),
              IsOkAndHolds(::testing::StrEq("file:///C:/tmp/|zarr2:")));
}

TEST(SpecTest, FromJsonString) {
  EXPECT_THAT(Spec::FromJson("file:///C:/tmp/|zarr"),
              MatchesStatus(absl::StatusCode::kInvalidArgument,
                            ".*unsupported URL scheme: .zarr.*"));
  // This relies on zarr2 being linked in.  If it is not linked in, then the
  // driver will not be found and the call will fail.
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(auto spec,
                                   Spec::FromJson("file:///C:/tmp/|zarr2"));

  EXPECT_THAT(spec.ToJson(),  //
              ::testing::Optional(MatchesJson({
                  {"driver", "zarr"},
                  {"kvstore",
                   {
                       {"driver", "file"},
                       {"path", "C:/tmp/"},
                   }},
              })));

  EXPECT_THAT(spec.ToUrl(),
              IsOkAndHolds(::testing::StrEq("file:///C:/tmp/|zarr2:")));
}

TEST(SpecSerializationTest, Invalid) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(auto invalid_spec,
                                   SerializationRoundTrip(Spec()));
  EXPECT_FALSE(invalid_spec.valid());
}

TEST(SpecSerializationTest, Valid) {
  TENSORSTORE_ASSERT_OK_AND_ASSIGN(
      auto spec,
      Spec::FromJson(
          {{"driver", "array"}, {"array", {1, 2, 3}}, {"dtype", "int32"}}));
  TestSerializationRoundTrip(spec);
}

}  // namespace
