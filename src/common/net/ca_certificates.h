// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <system_error>

#include "common/httplib.h"

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

namespace Common::Net {

// A portable build (AppImage, tar.gz) can carry an OpenSSL whose compiled-in certificate
// directory belongs to the machine that built it. Those directories do not exist on every
// distribution, so fall back to the system's own bundle when there is no embedded one.
template <typename Client>
void ConfigureCaCertificates(Client& client) {
#ifdef YUZU_BUNDLED_OPENSSL
    client.load_ca_cert_store(kCert, sizeof(kCert));
#elif !defined(_WIN32)
    for (const char* const bundle : {"/etc/ssl/certs/ca-certificates.crt",
                                     "/etc/pki/tls/certs/ca-bundle.crt",
                                     "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem",
                                     "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"}) {
        std::error_code error;
        if (std::filesystem::exists(bundle, error)) {
            client.set_ca_cert_path(bundle);
            break;
        }
    }
#else
    (void)client;
#endif
}

} // namespace Common::Net
