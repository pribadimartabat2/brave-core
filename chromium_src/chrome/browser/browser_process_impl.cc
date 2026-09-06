/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "build/build_config.h"
#include "extensions/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "brave/browser/extensions/brave_extensions_browser_client_impl.h"
#define ChromeExtensionsBrowserClient BraveExtensionsBrowserClientImpl
#endif

#if BUILDFLAG(IS_WIN)
#include "brave/browser/os_crypt/brave_portable_key_provider_win.h"

// Preserve Chromium's v10/v20 providers for legacy decryption, but make them
// decryption-only when Brave's explicit portable mode is active.
#define DPAPIKeyProvider BravePortableAwareDPAPIKeyProvider
#define AppBoundEncryptionProviderWin \
  BravePortableAwareAppBoundEncryptionProviderWin

// Chromium owns the provider vector, so a one-line upstream hook is used to
// append Brave's portable provider without duplicating PreMainMessageLoopRun.
#define BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER             \
  if (brave::os_crypt::IsPortableEncryptionRequested()) {                   \
    providers.emplace_back(std::make_pair(                                  \
        /*precedence=*/20u,                                                  \
        std::make_unique<brave::os_crypt::BravePortableKeyProvider>()));     \
  }
#endif  // BUILDFLAG(IS_WIN)

#include <chrome/browser/browser_process_impl.cc>

#if BUILDFLAG(IS_WIN)
#undef BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER
#undef AppBoundEncryptionProviderWin
#undef DPAPIKeyProvider
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#undef ChromeExtensionsBrowserClient
#endif
