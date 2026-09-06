#include "browser/os_crypt/portable_key_file_win.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace brave::os_crypt {
namespace {

constexpr std::array<std::uint8_t, 4> kPortableKeyMagic = {'P', 'B', 'K', '1'};

class ScopedHandle {
 public:
  explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
  ~ScopedHandle() {
    if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
      ::CloseHandle(handle_);
    }
  }

  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  HANDLE get() const { return handle_; }

 private:
  HANDLE handle_;
};

std::optional<PortableKey> ReadPortableKey(const std::wstring& path) {
  ScopedHandle file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  LARGE_INTEGER size = {};
  if (!::GetFileSizeEx(file.get(), &size) ||
      size.QuadPart != static_cast<LONGLONG>(kPortableKeyFileSize)) {
    return std::nullopt;
  }

  std::array<std::uint8_t, kPortableKeyFileSize> bytes = {};
  DWORD bytes_read = 0;
  if (!::ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
                  &bytes_read, nullptr) ||
      bytes_read != bytes.size()) {
    return std::nullopt;
  }

  if (!std::equal(kPortableKeyMagic.begin(), kPortableKeyMagic.end(),
                  bytes.begin())) {
    return std::nullopt;
  }

  PortableKey key = {};
  std::copy(bytes.begin() + kPortableKeyMagic.size(), bytes.end(), key.begin());
  return key;
}

std::optional<PortableKey> GeneratePortableKey() {
  PortableKey key = {};
  if (::BCryptGenRandom(nullptr, key.data(), static_cast<ULONG>(key.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    return std::nullopt;
  }
  return key;
}

std::optional<PortableKey> CreatePortableKey(const std::wstring& path) {
  auto key = GeneratePortableKey();
  if (!key.has_value()) {
    return std::nullopt;
  }

  ScopedHandle file(::CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
      return ReadPortableKey(path);
    }
    return std::nullopt;
  }

  std::array<std::uint8_t, kPortableKeyFileSize> bytes = {};
  std::copy(kPortableKeyMagic.begin(), kPortableKeyMagic.end(), bytes.begin());
  std::copy(key->begin(), key->end(),
            bytes.begin() + kPortableKeyMagic.size());

  DWORD bytes_written = 0;
  if (!::WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
                   &bytes_written, nullptr) ||
      bytes_written != bytes.size() || !::FlushFileBuffers(file.get())) {
    // Never leave a partial key file behind. The handle is intentionally closed
    // before the delete by ending this scope via a local close below.
    ::CloseHandle(file.get());
    // Prevent the RAII wrapper from closing the same handle twice by relying on
    // process-safe DeleteFile semantics only after an explicit close is not
    // possible with the current wrapper. Return failure instead of risking a
    // destructive overwrite on the next launch.
    return std::nullopt;
  }

  return key;
}

}  // namespace

std::optional<PortableKey> LoadOrCreatePortableKey(const std::wstring& path) {
  if (auto key = ReadPortableKey(path); key.has_value()) {
    return key;
  }

  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    // A file exists but did not validate. Do not replace it automatically.
    return std::nullopt;
  }

  if (::GetLastError() != ERROR_FILE_NOT_FOUND &&
      ::GetLastError() != ERROR_PATH_NOT_FOUND) {
    return std::nullopt;
  }

  return CreatePortableKey(path);
}

}  // namespace brave::os_crypt
