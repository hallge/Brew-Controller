Vendored from Espressif's esp-iot-solution, components/elf_loader 2.1.0 (Apache-2.0, LICENSE-Apache-2.0.txt),
unmodified. Used only by src/platform/AddonLoader.cpp (docs/ADDONS.md, "Add-ons as modules").

Kept: the loader core, ELF validation, the Xtensa relocations and the PSRAM adapter. Left out: the RISC-V and
ESP32-S2 parts, the dlopen layer, the ready-made libc/ESP-IDF symbol tables (the module gets only the short
allow-list in AddonLoader.cpp, through elf_set_symbol_resolver).

Configuration (what Kconfig would set: load into PSRAM, ESP32-S3 bus mirror) is the CONFIG_ELF_LOADER* defines in platformio.ini build_flags.
