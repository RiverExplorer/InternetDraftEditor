# Prebuilt Windows x64 static libraries — libxml2 + OpenSSL

Prebuilt static libraries and headers for Windows x64, so Doug does not have to
build libxml2 or OpenSSL from source. Everything here is **x64** and built for
the **/MD (dynamic CRT)** runtime — the default for a Visual Studio project.
All six `.lib` files were verified on a Linux VM with `objdump` (`pe-x86-64`)
and by checking their embedded `/DEFAULTLIB:"MSVCRT"` references.

**NOT compiled or link-tested on Windows.** Doug must link them into his
projects and confirm the build passes. The libxml2/OpenSSL combination is the
standard one for these versions, but the Windows link has not been run yet.

## What is in this folder

### libxml2 2.13.8 — static, /MD, x64
- **Source:** Conan Center (`conan-center`), package `libxml2/2.13.8`,
  downloaded with Conan 2.34.0. Built with MSVC 194 (VS 2022 17.10+).
  A C library built with MSVC 194 links cleanly into a v145 (VS 18) project:
  the C ABI is stable across toolsets and the CRT (/MD) matches.
- **Files here:**
  - `include/libxml/` — 46 headers (`parser.h`, `tree.h`, `xmlexports.h`, …).
    Copy the *contents* of this folder to `RelatedProjects/include/libxml/`,
    so code that includes `<libxml/parser.h>` finds it.
  - `lib/libxml2.lib` (4,884,028 bytes) — the static lib. It was named
    `libxml2_a.lib` in the Conan package; it is renamed here to `libxml2.lib`
    because that is the name Doug's compiler project links.
- **Dependencies — link these too.** This libxml2 was built with iconv and
  zlib support, so the program that links it must also link:
  - `lib/iconv.lib` (1,089,748) + `lib/charset.lib` (7,158) — libiconv 1.17,
    same Conan Center source, MSVC 194, /MD, x64.
    Headers: `include/iconv.h`, `include/libcharset.h`,
    `include/localcharset.h` — copy to `RelatedProjects/include/` (top level,
    since code includes `<iconv.h>`).
  - `lib/zlib.lib` (192,920) — zlib 1.3.1, same Conan Center source,
    MSVC 194, /MD, x64.
    Headers: `include/zlib.h`, `include/zconf.h` — copy to
    `RelatedProjects/include/` (top level, since code includes `<zlib.h>`).
  - `libxml2_a.lib` references `libiconv_open`/`libiconv_close` and zlib's
    `inflate`/`gzread` etc. — verified by inspecting the library.
- **LIBXML_STATIC must be defined.** The headers in `include/libxml/` do
  NOT define it themselves (`xmlexports.h` has `#ifdef LIBXML_STATIC`).
  Doug already handles this by editing `xmlexports.h` to define it — that
  edit must be re-applied to this copy of `xmlexports.h`, or `LIBXML_STATIC`
  must be added to the compiler project's Preprocessor Definitions.
  Without it, the link fails with `__imp_xml*` unresolved externals
  (the code expects the DLL import library instead of the static lib).
- **Link line for the compiler project** (in this order):
  `libxml2.lib iconv.lib charset.lib zlib.lib`
- **No LZMA.** This libxml2 was built with `lzma: False`, so there is no
  liblzma dependency to link.

### OpenSSL 3.5.9 (LTS) — static, /MD, x64
- **Source:** Shining Light Productions (slproweb), the well-known Windows
  OpenSSL distributor, listed on the OpenSSL project's own Binaries page.
  Installer: `Win64OpenSSL-3_5_9.exe`
  (https://slproweb.com/download/Win64OpenSSL-3_5_9.exe),
  297,484,663 bytes.
  SHA-256 verified against slproweb's official hashes file
  (https://github.com/slproweb/opensslhashes):
  `5fe338c81d62515df7292f16542a0eef2a29db3efc023cd5a8c08ec3c97027ae`
  The installer was extracted on Linux with `innoextract`; the original
  installer is kept in `downloads/Win64OpenSSL-3_5_9.exe`.
- **Files here:**
  - `include/openssl/` — 145 headers. Copy the whole folder to
    `RelatedProjects/include/openssl/`, so code that includes
    `<openssl/ssl.h>` finds it.
  - `lib/libssl_static.lib` (12,923,158) and
    `lib/libcrypto_static.lib` (59,159,802) — the static libs, from the
    installer's `lib/VC/x64/MD/` folder. **MD = /MD (dynamic CRT).**
    Their names are kept as-is (`libssl_static.lib`, `libcrypto_static.lib`).
- **Link line for a program that uses OpenSSL** (CoopLib / picotls):
  `libssl_static.lib libcrypto_static.lib ws2_32.lib crypt32.lib`
  (`ws2_32.lib` and `crypt32.lib` are Windows system libs; static OpenSSL
  needs them for sockets and the certificate store.)
- **MT variants:** if a project ever uses /MT (static CRT) instead of /MD,
  the matching static libs are in the installer's `lib/VC/x64/MT/` folder
  (`libssl_static.lib`, `libcrypto_static.lib`, referencing
  `/DEFAULTLIB:"LIBCMT"`). They are NOT included here — only the /MD pair,
  which matches Doug's projects. The installer in `downloads/` has them.
- **Debug variants** (`MDd` / `MTd`) are also in the installer, not here.
- **Version note:** OpenSSL 3.5.9 is the current 3.5 LTS. The MinGW-built
  `libpicotls-openssl.a` links against the OpenSSL 3.5 C API; the API is
  stable within the 3.5 series, so these 3.5.9 libs should link against it.
  Confirm on the Windows link.

## How to install into RelatedProjects

Copy into `RelatedProjects/` (the layout Doug uses):

| From this folder | To |
|---|---|
| `include/libxml/*` | `RelatedProjects/include/libxml/` |
| `include/openssl/*` | `RelatedProjects/include/openssl/` |
| `include/iconv.h`, `include/libcharset.h`, `include/localcharset.h` | `RelatedProjects/include/` |
| `include/zlib.h`, `include/zconf.h` | `RelatedProjects/include/` |
| `lib/libxml2.lib`, `lib/iconv.lib`, `lib/charset.lib`, `lib/zlib.lib` | `RelatedProjects/lib/` |
| `lib/libssl_static.lib`, `lib/libcrypto_static.lib` | `RelatedProjects/lib/` |

Then re-apply the `LIBXML_STATIC` edit to the new
`RelatedProjects/include/libxml/xmlexports.h` (or define `LIBXML_STATIC` in
the compiler project), and rebuild the compiler and CoopLib.

## CRT — the one thing that must match

Every `.lib` here is **/MD** (dynamic CRT). A Visual Studio project links
them only if the project itself uses /MD (the VS default: C/C++ →
Code Generation → Runtime Library → **Multi-threaded DLL (/MD)** in Release,
**Multi-threaded Debug DLL (/MDd)** in Debug). If a project is set to /MT,
linking these libs produces `LNK2038: mismatch detected for 'RuntimeLibrary'`
errors. In that case use the /MT variants from the slproweb installer
(`lib/VC/x64/MT/`) for OpenSSL, and fetch the /MT libxml2/iconv/zlib set
from Conan Center with `compiler.runtime=static` instead.

## Sources (for Doug's CDN backup)

- OpenSSL 3.5.9 installer (verified SHA-256, see above):
  https://slproweb.com/download/Win64OpenSSL-3_5_9.exe
  Hashes: https://github.com/slproweb/opensslhashes
- libxml2 2.13.8, libiconv 1.17, zlib 1.3.1 (Conan Center, MSVC 194, /MD,
  Release, static):
  https://conan.io/center/recipes/libxml2
  https://conan.io/center/recipes/libiconv
  https://conan.io/center/recipes/zlib
  (Fetched with Conan 2.34.0; the libxml2 package ID is
  `2426f62cbfb4eeb06786d714079ba6905afc6987`, and libiconv/zlib share
  `0d6dd492a7d31822b2f2686ec67bbaef586416a3`.)

## What was NOT used, and why

- **vcpkg** — banned by Doug; not used.
- **zlatkovic.com libxml binaries** — ancient (VS 6 era, msvcrt.dll, libxml2
  2.6/2.7); not usable.
- **mhils/libxml2-win-binaries** — last release 2022, libxml2 2.9.5; too old.
- **NuGet `libxml2.vcpkg` 2.14.5** — a vcpkg-produced package; excluded per
  Doug's vcpkg ban.
- **NuGet `libxml2-vc140-static-32_64` 2.9.4** — VS 2015 era, libxml2 2.9.4;
  too old.
- **FireDaemon OpenSSL** — listed by the OpenSSL project, but its
  `libssl.lib`/`libcrypto.lib` are DLL import libs, not static libs.
- **MSYS2 mingw-w64 libxml2** — MinGW-built `.a` files; usable in principle
  with MSVC but fragile (MinGW runtime references). Not needed: the Conan
  Center MSVC build above is the cleaner source.
