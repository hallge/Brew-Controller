#pragma once
// Task watchdog stand-in: a late control task is reported in the emulator
// log, it does not reset the emulated controller.
void esp_task_wdt_init(int timeoutS, bool panic);
void esp_task_wdt_add(void *task);
void esp_task_wdt_reset();
