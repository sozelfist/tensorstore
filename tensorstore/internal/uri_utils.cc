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

#include "tensorstore/internal/uri_utils.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "tensorstore/internal/ascii_set.h"

namespace tensorstore {
namespace internal {
namespace {

inline int HexDigitToInt(char c) {
  assert(absl::ascii_isxdigit(c));
  int x = static_cast<unsigned char>(c);
  if (x > '9') {
    x += 9;
  }
  return x & 0xf;
}

inline char IntToHexDigit(int x) {
  assert(x >= 0 && x < 16);
  return "0123456789ABCDEF"[x];
}

}  // namespace

void PercentEncodeReserved(std::string_view src, std::string& dest,
                           AsciiSet unreserved) {
  size_t num_escaped = 0;
  for (char c : src) {
    if (!unreserved.Test(c)) ++num_escaped;
  }
  if (num_escaped == 0) {
    dest = src;
    return;
  }
  dest.clear();
  dest.reserve(src.size() + 2 * num_escaped);
  for (char c : src) {
    if (unreserved.Test(c)) {
      dest += c;
    } else {
      dest += '%';
      dest += IntToHexDigit(static_cast<unsigned char>(c) / 16);
      dest += IntToHexDigit(static_cast<unsigned char>(c) % 16);
    }
  }
}

void PercentDecodeAppend(std::string_view src, std::string& dest) {
  dest.reserve(dest.size() + src.size());
  for (size_t i = 0; i < src.size();) {
    char c = src[i];
    char x, y;
    if (c != '%' || i + 2 >= src.size() ||
        !absl::ascii_isxdigit((x = src[i + 1])) ||
        !absl::ascii_isxdigit((y = src[i + 2]))) {
      dest += c;
      ++i;
      continue;
    }
    dest += static_cast<char>(HexDigitToInt(x) * 16 + HexDigitToInt(y));
    i += 3;
  }
}

namespace {
ParsedGenericUri ParseGenericUriImpl(std::string_view uri,
                                     std::string_view scheme_delimiter) {
  ParsedGenericUri result;
  const auto scheme_start = uri.find(scheme_delimiter);
  std::string_view uri_suffix;
  if (scheme_start == std::string_view::npos) {
    // No scheme
    uri_suffix = uri;
  } else {
    result.scheme = uri.substr(0, scheme_start);
    uri_suffix = uri.substr(scheme_start + scheme_delimiter.size());
  }
  const auto fragment_start = uri_suffix.find('#');
  const auto query_start = uri_suffix.substr(0, fragment_start).find('?');
  const auto path_end = std::min(query_start, fragment_start);
  // Note: Since substr clips out-of-range count, this works even if
  // `path_end == npos`.
  result.authority_and_path = uri_suffix.substr(0, path_end);

  if (const auto path_start = result.authority_and_path.find('/');
      path_start == 0 || result.authority_and_path.empty()) {
    result.authority = {};
    result.path = result.authority_and_path;
  } else if (path_start != std::string_view::npos) {
    result.authority = result.authority_and_path.substr(0, path_start);
    result.path = result.authority_and_path.substr(path_start);
  } else {
    result.authority = result.authority_and_path;
    result.path = {};
  }

  if (query_start != std::string_view::npos) {
    result.query =
        uri_suffix.substr(query_start + 1, fragment_start - query_start - 1);
  }
  if (fragment_start != std::string_view::npos) {
    result.fragment = uri_suffix.substr(fragment_start + 1);
  }
  return result;
}
}  // namespace

ParsedGenericUri ParseGenericUri(std::string_view uri) {
  return ParseGenericUriImpl(uri, "://");
}

ParsedGenericUri ParseGenericUriWithoutSlashSlash(std::string_view uri) {
  return ParseGenericUriImpl(uri, ":");
}

absl::Status EnsureNoQueryOrFragment(const ParsedGenericUri& parsed_uri) {
  if (!parsed_uri.query.empty()) {
    return absl::InvalidArgumentError("Query string not supported");
  }
  if (!parsed_uri.fragment.empty()) {
    return absl::InvalidArgumentError("Fragment identifier not supported");
  }
  return absl::OkStatus();
}

absl::Status EnsureNoPathOrQueryOrFragment(const ParsedGenericUri& parsed_uri) {
  if (!parsed_uri.authority_and_path.empty()) {
    return absl::InvalidArgumentError("Path not supported");
  }
  return EnsureNoQueryOrFragment(parsed_uri);
}

std::optional<HostPort> SplitHostPort(std::string_view host_port) {
  if (host_port.empty()) return std::nullopt;
  if (host_port[0] == '[') {
    // Parse a bracketed host, typically an IPv6 literal.
    const size_t rbracket = host_port.find(']', 1);
    if (rbracket == std::string_view::npos) {
      // Invalid: Unmatched [
      return std::nullopt;
    }
    if (!absl::StrContains(host_port.substr(1, rbracket - 1), ':')) {
      // Invalid: No colons in IPv6 literal
      return std::nullopt;
    }
    if (rbracket == host_port.size() - 1) {
      // [...]
      return HostPort{host_port, {}};
    }
    if (host_port[rbracket + 1] == ':') {
      if (host_port.rfind(':') != rbracket + 1) {
        // Invalid: multiple colons
        return std::nullopt;
      }
      // [...]:port
      return HostPort{host_port.substr(0, rbracket + 1),
                      host_port.substr(rbracket + 2)};
    }
    return std::nullopt;
  }

  // IPv4 or bare hostname.
  size_t colon = host_port.find(':');
  if (colon == std::string_view::npos ||
      host_port.find(':', colon + 1) != std::string_view::npos) {
    // 0 or 2 colons, assume a hostname.
    return HostPort{host_port, {}};
  }

  return HostPort{host_port.substr(0, colon), host_port.substr(colon + 1)};
}

}  // namespace internal
}  // namespace tensorstore
