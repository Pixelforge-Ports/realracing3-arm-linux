#pragma once

#include <stdbool.h>

/* Small file-backed control channel used by the RR3 MCP emulator. */
void rr3_control_init(const char *directory);
bool rr3_control_tick(long frame);
void rr3_control_after_draw(long frame, int width, int height);
void rr3_control_shutdown(long frame);
