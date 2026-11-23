# Prebuilt Windows x64 static libraries — libxslt + xmlsec

Prebuilt static libraries and headers for Windows x64, so Doug does not have to
build libxslt or xmlsec from source. Everything here is **x64** and built for
the **/MD (dynamic CRT)** runtime — the default for a Visual Studio project,
matching the libxml2/OpenSSL set already in `RelatedProjects` (libxml2 2.13.8,
libiconv 1.17, zlib 1.3.1, OpenSSL 3.5.9, all /MD, x64).
All four `.lib` files were verified on a Linux VM with `objdump` (`pe-x86-64`)
and by checking their embedded `/DEFAULTLIB:"MSVCRT"` references.

**NOT compiled or link-tested on Windows.** Doug must link them into his
projects and confirm the build passes. The libxslt/xmlsec combination is the
standard one for these versions, but the Windows link has not been run yet.

## What is in this folder

### libxslt 1.1.43 — static, /MD, x64
- **Source:** Conan Center (`conan-center`), package `libxslt/1.1.43`,
  downloaded with Conan 2.34.0. Built with MSVC 194 (VS 2022 17.10+).
  A C library built with MSVC 194 links cleanly into a v145 (VS 18) project:
  the C ABI is stable across toolsets and the CRT (/MD) matches.
- **Files here:**
  - `include/libxslt/` — 24 headers (`xslt.h`, `transform.h`, `xsltconfig.h`,
    `win32config.h`, …). Copy the *contents* of this folder to
    `RelatedProjects/include/libxslt/`, so code that includes
    `<libxslt/transform.h>` finds it.
  - `include/libexslt/` — 4 headers (`exslt.h`, `libexslt.h`, …). Copy to
    `RelatedProjects/include/libexslt/`. EXSLT is libxslt's extension
    library (common extension functions: dates, maths, strings, …).
  - `lib/libxslt_a.lib` (867,720 bytes) — the static lib.
  - `lib/libexslt_a.lib` (339,454 bytes) — the static EXSLT lib. Link it
    only if the code uses EXSLT functions; `libxslt_a.lib` alone is enough
    for plain XSLT.
- **Built against:** libxml2 2.13.x, libiconv 1.17, zlib 1.3.x — the same
  versions Doug already has. (Confirmed in the downloaded package's
  `conaninfo.txt`; see `libxslt-1.1.43-packages.txt`.)
- **Defines — required for static linking.** The headers declare the
  functions as `__declspec(dllimport)` unless these are defined, exactly
  like the `LIBXML_STATIC` problem Doug already hit with libxml2. Add to the
  project's Preprocessor Definitions (or edit the headers the same way he
  did for `xmlexports.h`):
  - `LIBXSLT_STATIC`
  - `LIBEXSLT_STATIC` (only if linking `libexslt_a.lib`)
  - plus `LIBXML_STATIC` for libxml2, as before.
  Without them the link fails with `__imp_xslt*` / `__imp_exslt*`
  unresolved externals.
- **Link line** (in this order — libxslt first, then its dependencies,
  which Doug already has in `RelatedProjects/lib`):
  `libxslt_a.lib libxml2.lib iconv.lib charset.lib zlib.lib ws2_32.lib`
  Add `libexslt_a.lib` right after `libxslt_a.lib` if using EXSLT.
- **No crypto.** This build was made with the `crypto` option off, so
  libxslt does not depend on libgcrypt or any crypto library. It references
  no gcrypt/NSS/GnuTLS symbols (verified by inspecting the library).

### xmlsec 1.3.6 — static, /MD, x64
- **Source:** Conan Center (`conan-center`), package `xmlsec/1.3.6`,
  downloaded with Conan 2.34.0. Built with MSVC 194. Same C-ABI/CRT note
  as libxslt above.
- **Files here:**
  - `include/xmlsec/` — 28 headers (`xmlsec.h`, `xmldsig.h`, `xmlenc.h`,
    `version.h`, …) plus the `include/xmlsec/openssl/` subfolder (6 headers:
    `app.h`, `crypto.h`, `evp.h`, `keysstore.h`, `symbols.h`, `x509.h`).
    Copy the *contents* of this folder to `RelatedProjects/include/xmlsec/`,
    so code that includes `<xmlsec/xmldsig.h>` and
    `<xmlsec/openssl/crypto.h>` finds them.
  - `lib/libxmlsec_a.lib` (3,038,372 bytes) — the static core library
    (XML Signature and XML Encryption, key management, transforms).
  - `lib/libxmlsec-openssl_a.lib` (2,233,444 bytes) — the static OpenSSL
    crypto engine. Link it alongside the core library; without it xmlsec
    has no crypto backend.
- **Built against:** libxml2 2.13.x and OpenSSL 3.5.x — the same versions
  Doug already has. (Confirmed in the downloaded package's `conaninfo.txt`;
  see `xmlsec-1.3.6-packages.txt`.)
- **Crypto engine: OpenSSL only.** This build has `with_openssl=True` and
  `with_nss/with_gcrypt/with_gnutls=False`. It uses OpenSSL 3.x
  (`openssl=300` in the build). The OpenSSL engine library references
  OpenSSL EVP/X509/BN symbols (verified by inspecting the library) — all of
  them live in **libcrypto**. It references **no `SSL_*` symbols**, so
  `libssl` is not needed for xmlsec (link it anyway if the program uses
  TLS elsewhere).
- **No XSLT in xmlsec.** This build has `with_xslt=False`, so xmlsec was
  built with `XMLSEC_NO_XSLT=1`: XSLT transforms inside XML signatures are
  not supported by this binary. That is the Conan Center default; a binary
  with XSLT support is not available prebuilt. If Doug later needs XSLT
  transforms in signatures, that is a from-source build of xmlsec.
- **Defines — required for static linking.** Add to the project's
  Preprocessor Definitions (same `__imp_xmlSec*` failure as libxml2/libxslt
  if omitted):
  - `XMLSEC_STATIC`
  - `XMLSEC_NO_XSLT=1`
  - `XMLSEC_NO_SIZE_T`
  - `XMLSEC_NO_GOST=1`
  - `XMLSEC_NO_CRYPTO_DYNAMIC_LOADING=1`
  - `XMLSEC_CRYPTO_OPENSSL=1`
  - plus `LIBXML_STATIC` for libxml2, as before.
- **Link line** (in this order — xmlsec first, then its dependencies):
  `libxmlsec_a.lib libxmlsec-openssl_a.lib libxml2.lib iconv.lib charset.lib zlib.lib libcrypto_static.lib crypt32.lib ws2_32.lib advapi32.lib user32.lib bcrypt.lib`
  (`libcrypto_static.lib` is from Doug's existing OpenSSL 3.5.9 set;
  `crypt32`, `ws2_32`, `advapi32`, `user32`, `bcrypt` are Windows system
  libs. `libssl_static.lib` is not needed by xmlsec.)

## Install layout (copy into RelatedProjects)

| This folder | Copy to |
|---|---|
| `include/libxslt/` | `RelatedProjects/include/libxslt/` |
| `include/libexslt/` | `RelatedProjects/include/libexslt/` |
| `include/xmlsec/` | `RelatedProjects/include/xmlsec/` |
| `lib/libxslt_a.lib` | `RelatedProjects/lib/libxslt_a.lib` |
| `lib/libexslt_a.lib` | `RelatedProjects/lib/libexslt_a.lib` |
| `lib/libxmlsec_a.lib` | `RelatedProjects/lib/libxmlsec_a.lib` |
| `lib/libxmlsec-openssl_a.lib` | `RelatedProjects/lib/libxmlsec-openssl_a.lib` |

## Versions NOT used, and why

- **libxslt 1.1.45 (newest)**: no prebuilt binary exists in Conan Center for
  MSVC 194 /MD / static. 1.1.43 is the newest version with a binary for this
  configuration. (1.1.45's own recipe and options are the same; only the
  prebuilt binary is missing.)
- **xmlsec 1.3.9 and 1.3.8 (newest)**: no prebuilt binary exists in Conan
  Center for MSVC 194 /MD / static. 1.3.6 is the newest version with a
  binary for this configuration.
- **DLL import libs excluded**: the xmlsec Conan package also ships
  `libxmlsec.lib` and `libxmlsec-openssl.lib` (DLL import libraries). They
  were left out on purpose — this set is static only, to match the rest of
  Doug's libraries. Do not mix them in.

## Verification performed (all on this Linux VM)

- **Architecture:** all four `.lib` files checked with `objdump` — all
  `pe-x86-64`. ✓
- **CRT:** all four checked for embedded `/DEFAULTLIB` — all reference
  `"MSVCRT"` = **/MD (dynamic CRT)**, none reference `LIBCMT`. ✓
- **Versions:** `LIBXSLT_VERSION 10143` in `libxslt/xsltconfig.h`;
  `XMLSEC_VERSION "1.3.6"` in `xmlsec/version.h`. ✓
- **Dependency versions:** the downloaded packages' `conaninfo.txt` records
  libxml2 2.13.Z, zlib 1.3.Z, libiconv 1.17.Z (libxslt) and libxml2 2.13.Z,
  OpenSSL 3.5.Z, zlib 1.3.Z (xmlsec) — matching Doug's existing set. ✓
- **Symbol inspection:** libxslt references only libxml2 + its own symbols
  (no gcrypt/NSS/GnuTLS); libxmlsec-openssl references OpenSSL EVP/X509/BN
  symbols (all in libcrypto) and zero `SSL_*` symbols, so libssl is not
  required; xmlsec core references no xslt symbols (XSLT support off). ✓
- **No vcpkg used anywhere.**

## What I could NOT verify — Doug must confirm on Windows

- **No Windows link or build was run.** The combination is the standard one
  for these versions, but the actual link into his projects has not been
  tested here (no MSVC, no Windows). Treat as "verified files, untested
  link" until his build passes.
- **The static defines must be set** (`LIBXSLT_STATIC`, `LIBEXSLT_STATIC`,
  `XMLSEC_STATIC` + the `XMLSEC_NO_*` / `XMLSEC_CRYPTO_OPENSSL` defines).
  Without them the link fails with `__imp_xslt*` / `__imp_xmlSec*`
  unresolved externals — the same failure mode as libxml2's
  `LIBXML_STATIC`.
- **CRT match:** all libs are /MD. If any of Doug's projects is set to /MT,
  linking fails with `LNK2038: mismatch detected for 'RuntimeLibrary'`.
- The MinGW-built `libpicotls-openssl.a` is unaffected by this delivery;
  nothing here changes the picoquic/OpenSSL arrangement.
