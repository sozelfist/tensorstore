// Copyright 2022 The TensorStore Authors
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

#include <stddef.h>
#include <stdint.h>

#include <array>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "riegeli/bytes/cord_reader.h"
#include "riegeli/bytes/cord_writer.h"
#include "tensorstore/array.h"
#include "tensorstore/driver/image/driver_impl.h"
#include "tensorstore/driver/registry.h"
#include "tensorstore/index.h"
#include "tensorstore/internal/image/bmp_reader.h"
#include "tensorstore/internal/image/image_info.h"
#include "tensorstore/internal/json_binding/json_binding.h"
#include "tensorstore/kvstore/auto_detect.h"
#include "tensorstore/util/result.h"
#include "tensorstore/util/span.h"
#include "tensorstore/util/status.h"

namespace tensorstore {
namespace internal_image_driver {
namespace {

using ::tensorstore::internal_image::BmpReader;
using ::tensorstore::internal_image::ImageInfo;

namespace jb = tensorstore::internal_json_binding;

struct BmpSpecialization {
  constexpr static char id[] = "bmp";
  constexpr static char kTransactionError[] =
      "\"bmp\" driver does not support transactions";

  constexpr static auto ApplyMembers = [](auto& x, auto f) {};

  constexpr static auto default_json_binder = jb::EmptyBinder;

  Result<absl::Cord> EncodeImage(ArrayView<const void, 3> array_xyc) const {
    return absl::UnimplementedError("\"bmp\" driver does not support writing");
  }

  Result<SharedArray<uint8_t, 3>> DecodeImage(absl::Cord value) {
    riegeli::CordReader buffer_reader(&value);
    BmpReader reader;
    TENSORSTORE_RETURN_IF_ERROR(reader.Initialize(&buffer_reader));
    ImageInfo info = reader.GetImageInfo();
    std::array<Index, 3> shape_yxc = {static_cast<Index>(info.height),
                                      static_cast<Index>(info.width),
                                      static_cast<Index>(info.num_components)};
    SharedArray<uint8_t, 3> array_yxc = AllocateArray<uint8_t>(shape_yxc);
    TENSORSTORE_RETURN_IF_ERROR(reader.Decode(tensorstore::span(
        reinterpret_cast<unsigned char*>(array_yxc.data()),
        array_yxc.num_elements() * array_yxc.dtype().size())));
    return array_yxc;
  }
};

const internal::DriverRegistration<ImageDriverSpec<BmpSpecialization>>
    bmp_driver_registration;

const ImageDriverSpec<BmpSpecialization>::UrlSchemeRegistration
    bmp_driver_url_registration;

const internal_kvstore::AutoDetectRegistration auto_detect_registration{
    internal_kvstore::AutoDetectFileSpec::PrefixSignature(BmpSpecialization::id,
                                                          "BM")};
}  // namespace
}  // namespace internal_image_driver
}  // namespace tensorstore
