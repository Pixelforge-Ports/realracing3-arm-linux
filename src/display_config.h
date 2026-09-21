#pragma once
#ifndef DISPLAY_CONFIG_NO_SDL
#include <SDL2/SDL.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace display_config {
inline bool valid(int w, int h) { return w >= 160 && h >= 160 && w <= 4096 && h <= 4096; }
inline bool select(const char *prefix, int panel_w, int panel_h, int &w, int &h) {
    char key[64]; std::snprintf(key, sizeof(key), "%s_RESOLUTION", prefix);
    const char *value = std::getenv(key);
    if (!value || !*value || !std::strcmp(value, "auto")) {
        if (valid(panel_w, panel_h)) { w = panel_w; h = panel_h; }
    } else {
        char extra; int parsed_w, parsed_h;
        if (std::sscanf(value, "%dx%d%c", &parsed_w, &parsed_h, &extra) != 2 || !valid(parsed_w, parsed_h)) {
            std::fprintf(stderr, "%s must be auto or WIDTHxHEIGHT (160..4096)\n", key);
            return false;
        }
        w = parsed_w; h = parsed_h;
    }
    return true;
}
inline void publish(const char *prefix, int w, int h, bool portrait) {
    char key[64], value[16];
    std::snprintf(key, sizeof(key), "%s_SCREEN_W", prefix);
    std::snprintf(value, sizeof(value), "%d", portrait ? h : w); setenv(key, value, 1);
    std::snprintf(key, sizeof(key), "%s_SCREEN_H", prefix);
    std::snprintf(value, sizeof(value), "%d", portrait ? w : h); setenv(key, value, 1);
}
#ifndef DISPLAY_CONFIG_NO_SDL
inline bool detect(const char *prefix, int &w, int &h, bool portrait) {
    SDL_DisplayMode mode{};
    int pw = w, ph = h;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) { pw = mode.w; ph = mode.h; }
    if (!select(prefix, pw, ph, w, h)) return false;
    publish(prefix, w, h, portrait);
    return true;
}
#endif
inline bool drawable(const char *prefix, int pw, int ph, int &w, int &h, bool portrait) {
    if (!select(prefix, pw, ph, w, h)) return false;
    publish(prefix, w, h, portrait);
    std::fprintf(stderr, "%s render=%dx%d display=%dx%d\n", prefix, w, h, pw, ph);
    return true;
}
}
