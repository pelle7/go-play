#pragma once

#include <stdint.h>
#include "esp_system.h"

extern bool config_speedup;

bool odroid_ui_menu(bool restart_menu);
void odroid_ui_debug_enter_loop();

void QuickSaveSetBuffer(void* data);
