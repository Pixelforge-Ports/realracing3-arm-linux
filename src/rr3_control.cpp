#include "rr3_control.h"

#include <SDL2/SDL.h>
#include <zlib.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "khronos/gles2.h"
#include "input_bridge.h"
#include "gl_stats.h"
#include "trace.h"

static char g_dir[4096];
static FILE *g_commands;
static long g_command_offset;
static bool g_quit;
static char g_screenshot[256];

static void write_status(const char *state, long frame)
{
    if (!g_dir[0])
        return;

    char path[4096];
    char temporary[4096];
    snprintf(path, sizeof(path), "%s/status.json", g_dir);
    snprintf(temporary, sizeof(temporary), "%s/status.json.tmp", g_dir);

    FILE *out = fopen(temporary, "w");
    if (!out)
        return;
    fprintf(out, "{\"state\":\"%s\",\"frame\":%ld}\n", state, frame);
    fclose(out);
    rename(temporary, path);
}

void rr3_control_init(const char *directory)
{
    if (!directory || !*directory)
        return;

    snprintf(g_dir, sizeof(g_dir), "%s", directory);
    char screenshot_dir[4096];
    char command_path[4096];
    snprintf(screenshot_dir, sizeof(screenshot_dir), "%s/screenshots", g_dir);
    snprintf(command_path, sizeof(command_path), "%s/commands", g_dir);
    mkdir(g_dir, 0755);
    mkdir(screenshot_dir, 0755);

    g_commands = fopen(command_path, "a+");
    if (!g_commands) {
        trace("RR3 control disabled: cannot open %s: %s",
              command_path, strerror(errno));
        g_dir[0] = '\0';
        return;
    }
    fseek(g_commands, 0, SEEK_END);
    g_command_offset = ftell(g_commands);
    write_status("ready", 0);
    trace("RR3 MCP control ready at %s", g_dir);
}

static void consume_command(const char *line)
{
    char command[32];
    char argument[256];
    command[0] = argument[0] = '\0';
    sscanf(line, "%31s %255[^\n]", command, argument);

    if (strcmp(command, "quit") == 0) {
        g_quit = true;
    } else if (strcmp(command, "screenshot") == 0 && argument[0]) {
        snprintf(g_screenshot, sizeof(g_screenshot), "%s", argument);
    } else if (strcmp(command, "cursor") == 0 && argument[0]) {
        float x = 0.0f, y = 0.0f;
        if (sscanf(argument, "%f %f", &x, &y) == 2) {
            android_input_cursor_set(x, y);
            trace("RR3 MCP input: cursor %.1f %.1f", x, y);
        }
    } else if (strcmp(command, "click") == 0) {
        bool down = strcmp(argument, "down") == 0;
        android_input_cursor_press(down);
        trace("RR3 MCP input: click %s", down ? "down" : "up");
    } else if (strcmp(command, "button") == 0 && argument[0]) {
        char name[32], phase[16];
        if (sscanf(argument, "%31s %15s", name, phase) == 2) {
            bool down = strcmp(phase, "down") == 0;
            android_input_inject_control(name, down);
            trace("RR3 MCP input: button %s %s", name, down ? "down" : "up");
        }
    } else if (strcmp(command, "stick") == 0 && argument[0]) {
        char name[32];
        float x = 0.0f, y = 0.0f;
        if (sscanf(argument, "%31s %f %f", name, &x, &y) == 3) {
            android_input_inject_stick(name, x, y);
            trace("RR3 MCP input: stick %s %.3f %.3f", name, x, y);
        }
    } else if (command[0]) {
        trace("RR3 MCP command queued: %s", line);
    }
}

bool rr3_control_tick(long frame)
{
    if (!g_commands)
        return true;

    fflush(g_commands);
    fseek(g_commands, g_command_offset, SEEK_SET);
    char line[512];
    while (fgets(line, sizeof(line), g_commands)) {
        g_command_offset = ftell(g_commands);
        consume_command(line);
    }
    write_status(g_quit ? "stopping" : "running", frame);
    return !g_quit;
}

static void write_u32(FILE *out, uint32_t value)
{
    fputc((int)(value >> 24), out);
    fputc((int)(value >> 16), out);
    fputc((int)(value >> 8), out);
    fputc((int)value, out);
}

static void png_chunk(FILE *out, const char name[4], const unsigned char *data,
                      size_t size)
{
    write_u32(out, (uint32_t)size);
    fwrite(name, 1, 4, out);
    if (size)
        fwrite(data, 1, size, out);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)name, 4);
    if (size)
        crc = crc32(crc, data, (uInt)size);
    write_u32(out, (uint32_t)crc);
}

static bool save_png(const char *path, int width, int height)
{
    using ReadPixels = void (*)(GLint, GLint, GLsizei, GLsizei, GLenum,
                                GLenum, void *);
    ReadPixels read_pixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
    if (!read_pixels || width <= 0 || height <= 0)
        return false;

    using GetIntegerv = void (*)(GLenum, GLint *);
    GetIntegerv get_integerv = (GetIntegerv)SDL_GL_GetProcAddress("glGetIntegerv");
    GLint framebuffer = -1;
    if (get_integerv)
        get_integerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    trace("capture source: framebuffer=%d", framebuffer);

    size_t rgba_size = (size_t)width * (size_t)height * 4;
    size_t raw_stride = (size_t)width * 4 + 1;
    size_t raw_size = raw_stride * (size_t)height;
    unsigned char *rgba = (unsigned char *)malloc(rgba_size);
    unsigned char *raw = (unsigned char *)malloc(raw_size);
    if (!rgba || !raw) {
        free(rgba);
        free(raw);
        return false;
    }

    read_pixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    size_t nonblack = 0;
    size_t nonzero_alpha = 0;
    unsigned int max_channel = 0;
    for (size_t i = 0; i < rgba_size; i += 4) {
        unsigned int v = (unsigned int)rgba[i] + rgba[i + 1] + rgba[i + 2];
        if (v != 0)
            ++nonblack;
        if (rgba[i + 3] != 0)
            ++nonzero_alpha;
        if (v > max_channel)
            max_channel = v;
    }
    trace("capture pixels: nonblack=%zu/%zu alpha=%zu/%zu max_rgb_sum=%u", nonblack,
          (size_t)width * (size_t)height, nonzero_alpha,
          (size_t)width * (size_t)height, max_channel);
    for (int y = 0; y < height; ++y) {
        unsigned char *dst = raw + (size_t)y * raw_stride;
        dst[0] = 0;
        const unsigned char *src = rgba + (size_t)(height - 1 - y) * width * 4;
        memcpy(dst + 1, src, (size_t)width * 4);
    }

    uLongf compressed_size = compressBound((uLong)raw_size);
    unsigned char *compressed = (unsigned char *)malloc(compressed_size);
    FILE *out = compressed ? fopen(path, "wb") : NULL;
    bool ok = false;
    if (out && compress2(compressed, &compressed_size, raw, (uLong)raw_size,
                         Z_BEST_SPEED) == Z_OK) {
        static const unsigned char signature[8] =
            {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        fwrite(signature, 1, sizeof(signature), out);
        unsigned char header[13] = {0};
        header[0] = (unsigned char)(width >> 24);
        header[1] = (unsigned char)(width >> 16);
        header[2] = (unsigned char)(width >> 8);
        header[3] = (unsigned char)width;
        header[4] = (unsigned char)(height >> 24);
        header[5] = (unsigned char)(height >> 16);
        header[6] = (unsigned char)(height >> 8);
        header[7] = (unsigned char)height;
        header[8] = 8;       /* bit depth */
        header[9] = 6;       /* RGBA */
        png_chunk(out, "IHDR", header, sizeof(header));
        png_chunk(out, "IDAT", compressed, compressed_size);
        png_chunk(out, "IEND", NULL, 0);
        ok = true;
    }
    if (out)
        fclose(out);
    free(compressed);
    free(raw);
    free(rgba);
    return ok;
}

void rr3_control_after_draw(long frame, int width, int height)
{
    if (!g_screenshot[0])
        return;

    char path[4096];
    snprintf(path, sizeof(path), "%s/screenshots/%s.png", g_dir, g_screenshot);
    bool ok = save_png(path, width, height);
    trace("RR3 MCP screenshot %s: %s", g_screenshot, ok ? "saved" : "failed");
    /* Dump the GL census here so a screenshot and the counters that produced
     * it carry the same name; correlating them by log position otherwise means
     * counting frames across thousands of trace lines. */
    gl_stats_mark(g_screenshot, frame);
    g_screenshot[0] = '\0';
}

void rr3_control_shutdown(long frame)
{
    if (g_commands) {
        write_status("stopped", frame);
        fclose(g_commands);
        g_commands = NULL;
    }
}
