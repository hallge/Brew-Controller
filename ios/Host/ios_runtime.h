#pragma once
// iOS side of the in-process controller's host: clock, RTC memory, thread priorities, data folder,
// and the CoreBluetooth radio. Plain C so Swift can call it through the bridging header.
// Implementation: ios_runtime.mm. Design: ios-parity/design.md sections 1 and 6.

#ifdef __cplusplus
extern "C" {
#endif

// Installs the host hooks (emu::g_hooks), creates the data folder, restores the RTC image, starts the
// 100 ms RTC saver and calls bbw_host_configure. demo != 0: Application Support/Controller-demo and
// the virtual radio; otherwise Application Support/Controller and the CoreBluetooth backend.
// Call once, on any thread, before bbw_host_start(); later calls do nothing.
void bbw_ios_prepare(int demo);

// The same on a folder of the caller's choosing (the unit tests use a temporary one). NULL: as above.
void bbw_ios_prepare_at(const char *dataDir, int demo);

// The folder in use (valid after prepare), "" before.
const char *bbw_ios_data_dir(void);

#ifdef __cplusplus
}
#endif
