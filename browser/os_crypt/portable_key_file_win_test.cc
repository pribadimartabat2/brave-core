#include "browser/os_crypt/portable_key_file_win.h"

#include <windows.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

std::filesystem::path MakeTempDir() {
  wchar_t temp_path[MAX_PATH];
  DWORD len = ::GetTempPathW(MAX_PATH, temp_path);
  assert(len > 0 && len < MAX_PATH);

  wchar_t temp_file[MAX_PATH];
  UINT ok = ::GetTempFileNameW(temp_path, L"brp", 0, temp_file);
  assert(ok != 0);
  ::DeleteFileW(temp_file);
  std::filesystem::create_directory(temp_file);
  return std::filesystem::path(temp_file);
}

std::vector<unsigned char> ReadAll(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(input),
                                    std::istreambuf_iterator<char>());
}

void WriteAll(const std::filesystem::path& path,
              const std::vector<unsigned char>& bytes) {
  HANDLE file = ::CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
  assert(file != INVALID_HANDLE_VALUE);

  DWORD bytes_written = 0;
  const BOOL wrote = ::WriteFile(file, bytes.data(),
                                 static_cast<DWORD>(bytes.size()),
                                 &bytes_written, nullptr);
  const BOOL flushed = wrote ? ::FlushFileBuffers(file) : FALSE;
  ::CloseHandle(file);

  assert(wrote != FALSE);
  assert(bytes_written == static_cast<DWORD>(bytes.size()));
  assert(flushed != FALSE);
}

void TestCreatesAndReusesSamePortableKey() {
  const auto dir = MakeTempDir();
  const auto path = dir / L"Portable Encryption Key";

  const auto first = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(first.has_value());

  const auto second = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(second.has_value());
  assert(*first == *second);

  const auto bytes = ReadAll(path);
  assert(bytes.size() == brave::os_crypt::kPortableKeyFileSize);
  assert(bytes[0] == 'P' && bytes[1] == 'B' && bytes[2] == 'K' &&
         bytes[3] == '1');

  std::filesystem::remove_all(dir);
}

void TestRejectsMalformedExistingKeyWithoutReplacingIt() {
  const auto dir = MakeTempDir();
  const auto path = dir / L"Portable Encryption Key";

  const std::vector<unsigned char> malformed = {'b', 'a', 'd'};
  WriteAll(path, malformed);

  const auto result = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(!result.has_value());
  assert(ReadAll(path) == malformed);

  std::filesystem::remove_all(dir);
}

void TestMissingInitializedKeyFailsClosedInsteadOfRegenerating() {
  const auto dir = MakeTempDir();
  const auto path = dir / L"Portable Encryption Key";
  const auto state_path = std::filesystem::path(path.wstring() + L".state");

  const auto first = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(first.has_value());
  assert(std::filesystem::exists(state_path));

  std::filesystem::remove(path);
  assert(!std::filesystem::exists(path));

  const auto missing = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(!missing.has_value());
  assert(!std::filesystem::exists(path));

  std::filesystem::remove_all(dir);
}

void TestRejectsSameSizeKeyMutationAfterInitialization() {
  const auto dir = MakeTempDir();
  const auto path = dir / L"Portable Encryption Key";

  const auto first = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(first.has_value());

  auto bytes = ReadAll(path);
  assert(bytes.size() == brave::os_crypt::kPortableKeyFileSize);
  bytes.back() ^= 0x01;
  WriteAll(path, bytes);
  assert(ReadAll(path) == bytes);

  const auto changed = brave::os_crypt::LoadOrCreatePortableKey(path.wstring());
  assert(!changed.has_value());

  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  TestCreatesAndReusesSamePortableKey();
  TestRejectsMalformedExistingKeyWithoutReplacingIt();
  TestMissingInitializedKeyFailsClosedInsteadOfRegenerating();
  TestRejectsSameSizeKeyMutationAfterInitialization();
  std::cout << "portable_key_file_win_test: PASS\n";
  return 0;
}
