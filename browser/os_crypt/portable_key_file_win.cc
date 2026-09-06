#include "browser/os_crypt/portable_key_file_win.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

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
      bytes_read != static_cast<DWORD>(bytes.size())) {
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
      bytes_written != static_cast<DWORD>(bytes.size()) ||
      !::FlushFileBuffers(file.get())) {
    // Fail closed. A partial file, if any, is deliberately not overwritten on
    // the next launch; LoadOrCreatePortableKey will reject it for recovery.
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

  const DWORD error = ::GetLastError();
  if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
    return std::nullopt;
  }

  return CreatePortableKey(path);
}

}  // namespace brave::os_crypt
