# ScaleWire

Scale protocol tables and shared parsing helpers for Brew Controller, a clean-room replacement for
`lib/EspressoScaleProtocol`.

- Written only from `research/obbw-clean-spec.md` (or an extract of it), without reading the Open
  Brew by Weight files, on 2026-11-10. Who wrote what, and what each party saw, is in
  `research/clean-room-record.md`.
- Every family is here: Acaia (old and new protocol), Felicita, AtomHeart Eclair, Decent, DiFluid,
  MyScale, WeighMyBru, Varia AKU and Eureka Precisa. `Registry.cpp` is the project's own registry,
  moved here.
- `BBW_CLEAN=1 test/host/run.sh` builds the host tests against it. Digests in
  `test/host/test_clean.inc` show each table behaves exactly as the old one.
- The firmware still builds `lib/EspressoScaleProtocol` (`lib_ignore = ScaleWire` in
  `platformio.ini`) until the owner switches it over.
- Licence: written for Brew Controller; licence to be decided by the owner (intended to be
  dual-licensable).
