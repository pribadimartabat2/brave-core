#include "browser/os_crypt/portable_key_file_win.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace brave::os_crypt {
namespace {

constexpr std::array<std::uint8_t, 4> kPortableKeyMagic = {'P', 'B', 'K', '1'};
constexpr std::array<std::uint8_t, 4> kPortableStateMagicV1 = {'P', 'B', 'S', '1'};
constexpr std::array<std::uint8_t, 4> kPortableStateMagicV2 = {'P', 'B', 'S', '2'};
constexpr std::size_t kPortableStateFileSize = 12;
constexpr std::uint64_t kFnv1aOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnv1aPrime = 1099511628211ull;

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

class ScopedMemoryWipe {
 public:
  ScopedMemoryWipe(void* data, std::size_t size) : data_(data), size_(size) {}
  ~ScopedMemoryWipe() {
    if (data_ && size_ != 0) {
      ::SecureZeroMemory(data_, size_);
    }
  }

  ScopedMemoryWipe(const ScopedMemoryWipe&) = delete;
  ScopedMemoryWipe& operator=(const ScopedMemoryWipe&) = delete;

 private:
  void* data_;
  std::size_t size_;
};

enum class PortableStateStatus {
  kMissing,
  kValid,
  kLegacy,
  kInvalid,
};

std::wstring PortableStatePath(const std::wstring& path) {
  return path + L".state";
}

std::uint64_t PortableKeyFingerprint(const PortableKey& key) {
  std::uint64_t hash = kFnv1aOffsetBasis;
  for (const std::uint8_t byte : key) {
    hash ^= byte;
    hash *= kFnv1aPrime;
  }
  return hash;
}

std::array<std::uint8_t, kPortableStateFileSize> PortableStateBytes(
    const PortableKey& key) {
  std::array<std::uint8_t, kPortableStateFileSize> bytes = {};
  std::copy(kPortableStateMagicV2.begin(), kPortableStateMagicV2.end(),
            bytes.begin());

  const std::uint64_t fingerprint = PortableKeyFingerprint(key);
  for (std::size_t i = 0; i < sizeof(fingerprint); ++i) {
    bytes[kPortableStateMagicV2.size() + i] =
        static_cast<std::uint8_t>((fingerprint >> (i * 8)) & 0xffu);
  }
  return bytes;
}

PortableStateStatus ReadPortableState(const std::wstring& path,
                                      const PortableKey& key) {
  ScopedHandle file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return PortableStateStatus::kMissing;
    }
    return PortableStateStatus::kInvalid;
  }

  LARGE_INTEGER size = {};
  if (!::GetFileSizeEx(file.get(), &size)) {
    return PortableStateStatus::kInvalid;
  }

  if (size.QuadPart == static_cast<LONGLONG>(kPortableStateMagicV1.size())) {
    std::array<std::uint8_t, kPortableStateMagicV1.size()> legacy = {};
    DWORD bytes_read = 0;
    if (!::ReadFile(file.get(), legacy.data(),
                    static_cast<DWORD>(legacy.size()), &bytes_read, nullptr) ||
        bytes_read != static_cast<DWORD>(legacy.size())) {
      return PortableStateStatus::kInvalid;
    }
    return legacy == kPortableStateMagicV1 ? PortableStateStatus::kLegacy
                                           : PortableStateStatus::kInvalid;
  }

  if (size.QuadPart != static_cast<LONGLONG>(kPortableStateFileSize)) {
    return PortableStateStatus::kInvalid;
  }

  std::array<std::uint8_t, kPortableStateFileSize> bytes = {};
  DWORD bytes_read = 0;
  if (!::ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
                  &bytes_read, nullptr) ||
      bytes_read != static_cast<DWORD>(bytes.size())) {
    return PortableStateStatus::kInvalid;
  }

  if (!std::equal(kPortableStateMagicV2.begin(), kPortableStateMagicV2.end(),
                  bytes.begin())) {
    return PortableStateStatus::kInvalid;
  }

  return bytes == PortableStateBytes(key) ? PortableStateStatus::kValid
                                          : PortableStateStatus::kInvalid;
}

bool WritePortableState(const std::wstring& path,
                        const PortableKey& key,
                        DWORD creation_disposition) {
  ScopedHandle file(::CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, creation_disposition,
      FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    return false;
  }

  const auto bytes = PortableStateBytes(key);
  DWORD bytes_written = 0;
  return ::WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
                     &bytes_written, nullptr) &&
         bytes_written == static_cast<DWORD>(bytes.size()) &&
         ::FlushFileBuffers(file.get());
}

bool EnsurePortableState(const std::wstring& path, const PortableKey& key) {
  const PortableStateStatus status = ReadPortableState(path, key);
  if (status == PortableStateStatus::kValid) {
    return true;
  }
  if (status == PortableStateStatus::kInvalid) {
    return false;
  }
  if (status == PortableStateStatus::kLegacy) {
    // Upgrade the earlier presence-only marker while the valid key is present.
    return WritePortableState(path, key, CREATE_ALWAYS);
  }

  if (WritePortableState(path, key, CREATE_NEW)) {
    return true;
  }

  // Another process may have won the CREATE_NEW race.
  return ReadPortableState(path, key) == PortableStateStatus::kValid;
}

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
  ScopedMemoryWipe wipe_bytes(bytes.data(), bytes.size());
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
    ::SecureZeroMemory(key->data(), key->size());
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
      return ReadPortableKey(path);
    }
    return std::nullopt;
  }

  std::array<std::uint8_t, kPortableKeyFileSize> bytes = {};
  ScopedMemoryWipe wipe_bytes(bytes.data(), bytes.size());
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
    ::SecureZeroMemory(key->data(), key->size());
    return std::nullopt;
  }

  return key;
}

}  // namespace

std::optional<PortableKey> LoadOrCreatePortableKey(const std::wstring& path) {
  const std::wstring state_path = PortableStatePath(path);

  if (auto key = ReadPortableKey(path); key.has_value()) {
    // The state file binds initialization to this specific key fingerprint.
    // A legacy presence-only marker is upgraded only while the valid key is
    // available; a mismatching fingerprint fails closed.
    if (!EnsurePortableState(state_path, *key)) {
      ::SecureZeroMemory(key->data(), key->size());
      return std::nullopt;
    }
    return key;
  }

  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    // A key file exists but did not validate. Do not replace it automatically.
    return std::nullopt;
  }

  const DWORD error = ::GetLastError();
  if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
    return std::nullopt;
  }

  const DWORD state_attributes = ::GetFileAttributesW(state_path.c_str());
  if (state_attributes != INVALID_FILE_ATTRIBUTES) {
    // This profile was already initialized for portable encryption. A missing
    // key must never be replaced with a new key because existing brp1 data
    // would become silently undecryptable.
    return std::nullopt;
  }

  const DWORD state_error = ::GetLastError();
  if (state_error != ERROR_FILE_NOT_FOUND && state_error != ERROR_PATH_NOT_FOUND) {
    return std::nullopt;
  }

  auto key = CreatePortableKey(path);
  if (!key.has_value()) {
    return std::nullopt;
  }

  if (!EnsurePortableState(state_path, *key)) {
    ::SecureZeroMemory(key->data(), key->size());
    return std::nullopt;
  }

  return key;
}

}  // namespace brave::os_crypt
