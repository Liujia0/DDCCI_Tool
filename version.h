#pragma once

// =============================================================================
//  Single source of version — edit ONLY this file to change the version.
//  All other files (rc, cpp, html, js) derive from these three numbers.
// =============================================================================
#define VERSION_MAJOR 0
#define VERSION_MINOR 9
#define VERSION_PATCH 1

// ---------------------------------------------------------------------------
//  Derived macros — do NOT edit below this line
// ---------------------------------------------------------------------------

// Stringification helpers (narrow)
#define _VER_STRINGIFY(x) #x
#define VER_STRINGIFY(x)  _VER_STRINGIFY(x)

// Stringification helpers (wide) — use L#x to stringify and make wide literal
#define _VER_WSTRINGIFY(x) L#x
#define VER_WSTRINGIFY(x)  _VER_WSTRINGIFY(x)

// Dot-separated string: "0.8.3" (for char* contexts)
#define VERSION_DOT VER_STRINGIFY(VERSION_MAJOR) "." \
                    VER_STRINGIFY(VERSION_MINOR) "." \
                    VER_STRINGIFY(VERSION_PATCH)

// Wide dot-separated string: L"0.8.3" (for wchar_t* contexts)
#define VERSION_WDOT VER_WSTRINGIFY(VERSION_MAJOR) L"." \
                     VER_WSTRINGIFY(VERSION_MINOR) L"." \
                     VER_WSTRINGIFY(VERSION_PATCH)

// Dot-separated string with .0 suffix: "0.8.3.0" (for RC string table)
#define VERSION_DOT4 VER_STRINGIFY(VERSION_MAJOR) "." \
                     VER_STRINGIFY(VERSION_MINOR) "." \
                     VER_STRINGIFY(VERSION_PATCH) ".0"

// Comma-separated for FILEVERSION/PRODUCTVERSION binary fields
// RC requires a single macro expansion, so we use token-pasting (##)
#define _VER_COMMA4(a,b,c,d) a##,##b##,##c##,##d
#define VER_COMMA4(a,b,c,d)  _VER_COMMA4(a,b,c,d)
#define VERSION_COMMA VER_COMMA4(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, 0)
