// Copyright 2025 The TensorStore Authors
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

#include "tensorstore/tscli/search_command.h"

#include <iostream>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "tensorstore/context.h"
#include "tensorstore/kvstore/spec.h"
#include "tensorstore/tscli/command.h"
#include "tensorstore/tscli/lib/ts_search.h"
#include "tensorstore/util/json_absl_flag.h"

namespace tensorstore {
namespace cli {

SearchCommand::SearchCommand() : Command("search", "Search for TensorStores") {
  parser().AddBoolOption("--brief", "Brief", [this]() { brief_ = true; });
  parser().AddBoolOption("-b", "Brief", [this]() { brief_ = true; });
  parser().AddBoolOption("--full", "Full", [this]() { brief_ = false; });
  parser().AddBoolOption("-f", "Full", [this]() { brief_ = false; });

  auto parse_spec = [this](std::string_view value) {
    tensorstore::JsonAbslFlag<tensorstore::kvstore::Spec> spec;
    std::string error;
    if (!AbslParseFlag(value, &spec, &error)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid spec: ", value, " ", error));
    }
    specs_.push_back(spec.value);
    return absl::OkStatus();
  };

  parser().AddLongOption("--source", "Source kvstore spec", parse_spec);
  parser().AddPositionalArgs("kvstore spec", "Source kvstore spec", parse_spec);
}

absl::Status SearchCommand::Run(Context::Spec context_spec) {
  tensorstore::Context context(context_spec);

  absl::Status status;
  for (const auto& spec : specs_) {
    status.Update(TsSearch(context, spec, brief_, std::cout));
  }
  return status;
}

}  // namespace cli
}  // namespace tensorstore
