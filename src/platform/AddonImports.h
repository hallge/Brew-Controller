#pragma once
// What an add-on module may import from the firmware (docs/ADDONS.md, "Add-ons as modules"): the names, and
// what each is on the board. platform/AddonLoader.cpp builds its export table from this list; the host tests
// read the same list to check that every name in the documentation is exported. Plain text only, so it can
// be included anywhere (the address expressions are only expanded by the loader).
//
// A module is linked with -nostdlib: libgcc (64-bit integer and soft-float helpers, __udivmoddi4, __divdf3,
// ...) is linked into the module itself, so it is not listed here. Add a name only when a module really
// imports it; anything else makes that module fail to load with the name shown.
//
//   X(name, address of the function). Overloaded C++ declarations are picked with an explicit signature.

// Formatting and parsing numbers.
#define BBW_ADDON_IMPORTS_TEXT(X)                                                                   \
  X(snprintf, &snprintf)                                                                            \
  X(vsnprintf, &vsnprintf)                                                                          \
  X(strtod, &strtod)                                                                                \
  X(atof, &atof)                                                                                    \
  X(atoi, &atoi)                                                                                    \
  X(atol, &atol)

// Strings and memory (strchr, strstr and memchr are overloaded in C++, hence the signatures).
#define BBW_ADDON_IMPORTS_MEMORY(X)                                                                 \
  X(strlen, &strlen)                                                                                \
  X(strcmp, &strcmp)                                                                                \
  X(strncmp, &strncmp)                                                                              \
  X(strncasecmp, &strncasecmp)                                                                      \
  X(strstr, static_cast<char *(*)(const char *, const char *)>(&strstr))                            \
  X(strchr, static_cast<char *(*)(const char *, int)>(&strchr))                                     \
  X(strtok, &strtok)                                                                                \
  X(memcpy, &memcpy)                                                                                \
  X(memmove, &memmove)                                                                              \
  X(memset, &memset)                                                                                \
  X(memcmp, &memcmp)                                                                                \
  X(memchr, static_cast<void *(*)(const void *, int, size_t)>(&memchr))                             \
  X(strcpy, &strcpy)                                                                                \
  X(strncpy, &strncpy)

// Single-precision maths (the chip has a single-precision FPU; double maths comes from libgcc).
#define BBW_ADDON_IMPORTS_MATH(X)                                                                   \
  X(floorf, static_cast<float (*)(float)>(&floorf))                                                 \
  X(fabsf, static_cast<float (*)(float)>(&fabsf))                                                   \
  X(sqrtf, static_cast<float (*)(float)>(&sqrtf))                                                   \
  X(fminf, static_cast<float (*)(float, float)>(&fminf))

#define BBW_ADDON_IMPORTS(X)     \
  BBW_ADDON_IMPORTS_TEXT(X)      \
  BBW_ADDON_IMPORTS_MEMORY(X)    \
  BBW_ADDON_IMPORTS_MATH(X)
