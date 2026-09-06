#ifndef BRAVE_BROWSER_OS_CRYPT_BRAVE_PORTABLE_KEY_PROVIDER_WIN_H_
#define BRAVE_BROWSER_OS_CRYPT_BRAVE_PORTABLE_KEY_PROVIDER_WIN_H_

#include "chrome/browser/os_crypt/app_bound_encryption_provider_win.h"
#include "components/os_crypt/async/browser/dpapi_key_provider.h"
#include "components/os_crypt/async/browser/key_provider.h"

namespace brave::os_crypt {

// Returns true only for the explicit portable-encryption mode historically
// selected by Brave's --disable-encryption-win switch.
bool IsPortableEncryptionRequested();

// Provides the active OSCryptAsync key for portable profiles. The key is kept
// inside the user-data directory so the same encrypted profile can be opened on
// another Windows installation without depending on that machine's DPAPI key.
class BravePortableKeyProvider : public os_crypt_async::KeyProvider {
 public:
  BravePortableKeyProvider();
  ~BravePortableKeyProvider() override;

  BravePortableKeyProvider(const BravePortableKeyProvider&) = delete;
  BravePortableKeyProvider& operator=(const BravePortableKeyProvider&) = delete;

  void GetKey(KeyCallback callback) override;
  bool UseForEncryption() override;
};

}  // namespace brave::os_crypt

namespace os_crypt_async {

// In portable mode legacy DPAPI remains available for decrypting v10 data, but
// must not be selected for new encryption. Outside portable mode its upstream
// behavior is preserved.
class BravePortableAwareDPAPIKeyProvider : public DPAPIKeyProvider {
 public:
  using DPAPIKeyProvider::DPAPIKeyProvider;
  ~BravePortableAwareDPAPIKeyProvider() override = default;

 private:
  bool UseForEncryption() override;
};

// App-Bound remains available for decrypting existing v20 data on the machine
// that can unlock it, allowing Chromium's should_reencrypt path to migrate data
// to the portable provider. It is never used for new writes in portable mode.
class BravePortableAwareAppBoundEncryptionProviderWin
    : public AppBoundEncryptionProviderWin {
 public:
  using AppBoundEncryptionProviderWin::AppBoundEncryptionProviderWin;
  ~BravePortableAwareAppBoundEncryptionProviderWin() override = default;

  bool UseForEncryption() override;
};

}  // namespace os_crypt_async

#endif  // BRAVE_BROWSER_OS_CRYPT_BRAVE_PORTABLE_KEY_PROVIDER_WIN_H_
