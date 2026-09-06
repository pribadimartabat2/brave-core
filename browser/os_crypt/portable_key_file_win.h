#ifndef BRAVE_BROWSER_OS_CRYPT_PORTABLE_KEY_FILE_WIN_H_
#define BRAVE_BROWSER_OS_CRYPT_PORTABLE_KEY_FILE_WIN_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace brave::os_crypt {

inline constexpr std::size_t kPortableKeySize = 32;
inline constexpr std::size_t kPortableKeyFileSize = 4 + kPortableKeySize;
using PortableKey = std::array<std::uint8_t, kPortableKeySize>;

// Loads the portable profile key from |path|, or creates it atomically if it
// does not exist. Existing malformed files are never replaced automatically.
std::optional<PortableKey> LoadOrCreatePortableKey(const std::wstring& path);

}  // namespace brave::os_crypt

#endif  // BRAVE_BROWSER_OS_CRYPT_PORTABLE_KEY_FILE_WIN_H_
