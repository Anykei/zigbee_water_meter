// Local fallback version. GitHub Actions overwrites this file during CI builds.
#ifndef INCLUDE_VERSION_H_
#define INCLUDE_VERSION_H_

#include <string_view>

namespace firmware::version {

inline constexpr std::string_view kFirmwareVersion = "0.0.0-dev";
inline constexpr std::string_view kGitSha = "local";
inline constexpr std::string_view kBuildTimestamp = "local";
inline constexpr int kMajor = 0;
inline constexpr int kMinor = 0;
inline constexpr int kPatch = 0;

}  // namespace firmware::version

#endif  // INCLUDE_VERSION_H_
