#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace keystats {

int RunApplication(HINSTANCE instance, bool start_in_background, int show_command);

}  // namespace keystats
