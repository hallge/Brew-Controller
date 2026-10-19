#pragma once
// The firmware's version. A release build gets it from the tag it was built
// from (.github/workflows/release.yml passes -DBBW_VERSION); anything built
// by hand says so.
#ifndef BBW_VERSION
#define BBW_VERSION "dev"
#endif
#ifdef BBW_EMULATOR
#define BBW_BUILT "PC emulator"
#else
#define BBW_BUILT __DATE__ " " __TIME__
#endif
