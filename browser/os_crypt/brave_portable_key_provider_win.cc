#include "browser/os_crypt/brave_portable_key_provider_win.h"

#include <windows.h>

#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "brave/browser/os_crypt/portable_key_file_win.h"
#include "brave/components/constants/brave_switches.h"
#include "chrome/common/chrome_switches.h"
#include "components/os_crypt/async/common/algorithm.mojom.h"
#include "components/os_crypt/async/common/encryptor.h"

namespace brave::os_crypt {
namespace {

// Custom prefix avoids colliding with Chromium's v10 (DPAPI) and v20
// (App-Bound) ciphertext formats.
constexpr char kPortableDataPrefix[] = "brp1";
constexpr base::FilePath::CharType kPortableKeyFilename[] =
    FILE_PATH_LITERAL("Portable Encryption Key");

}  // namespace

bool IsPortableEncryptionRequested() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableEncryptionWin);
}

BravePortableKeyProvider::BravePortableKeyProvider() = default;
BravePortableKeyProvider::~BravePortableKeyProvider() = default;

void BravePortableKeyProvider::GetKey(KeyCallback callback) {
  if (!IsPortableEncryptionRequested()) {
    std::move(callback).Run(
        kPortableDataPrefix,
        base::unexpected(KeyError::kPermanentlyUnavailable));
    return;
  }

  const base::FilePath user_data_dir =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kUserDataDir);
  if (user_data_dir.empty()) {
    // Portable mode without an explicit portable profile path must never fall
    // back to a machine-bound key.
    std::move(callback).Run(
        kPortableDataPrefix,
        base::unexpected(KeyError::kTemporarilyUnavailable));
    return;
  }

  auto portable_key =
      LoadOrCreatePortableKey(user_data_dir.Append(kPortableKeyFilename).value());
  if (!portable_key.has_value()) {
    // Treat corruption or an unwritable portable key as recoverable. The v10
    // and v20 providers remain in the key ring for decryption, while their
    // portable-aware wrappers prevent machine-bound fallback encryption.
    std::move(callback).Run(
        kPortableDataPrefix,
        base::unexpected(KeyError::kTemporarilyUnavailable));
    return;
  }

  os_crypt_async::Encryptor::Key key(
      *portable_key, os_crypt_async::mojom::Algorithm::kAES256GCM);
  ::SecureZeroMemory(portable_key->data(), portable_key->size());
  std::move(callback).Run(kPortableDataPrefix, std::move(key));
}

bool BravePortableKeyProvider::UseForEncryption() {
  return IsPortableEncryptionRequested();
}

}  // namespace brave::os_crypt

namespace os_crypt_async {

bool BravePortableAwareDPAPIKeyProvider::UseForEncryption() {
  // Upstream DPAPI always returns true. Preserve that behavior unless explicit
  // portable mode is active.
  return !brave::os_crypt::IsPortableEncryptionRequested();
}

bool BravePortableAwareAppBoundEncryptionProviderWin::UseForEncryption() {
  if (brave::os_crypt::IsPortableEncryptionRequested()) {
    return false;
  }
  return AppBoundEncryptionProviderWin::UseForEncryption();
}

}  // namespace os_crypt_async
