#pragma once
// Functions the firmware calls that only a host program (the iPhone app) provides. The board and
// the PC emulator never define BBW_HOST_IOS and so never reference these.
#ifdef BBW_HOST_IOS
// The controller asked to restart (a restore, a mode switch). The host finishes the response in
// hand, stops the loop and tells the user; it does not return control to the firmware's restart.
void hostRequestRestart();
#endif
