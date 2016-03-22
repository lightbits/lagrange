#pragma once
#include "SDL_opengl.h"
#include "SDL.h"
#include "lib/imgui/imgui.h"
#include "lib/imgui/stb_truetype.h"
#include "lib/imgui/stb_rect_pack.h"
#include "lib/so_math.h"
#include <stdint.h>
typedef float       r32;
typedef uint64_t    u64;
typedef uint32_t    u32;
typedef uint16_t    u16;
typedef uint8_t     u08;
typedef int8_t      s08;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
#define global static
#define persist static

struct VideoMode
{
    int width;
    int height;
    int gl_major;
    int gl_minor;
    int double_buffer;
    int depth_bits;
    int stencil_bits;
    int multisamples;

    // 0 for immediate updates, 1 for updates synchronized with the
    // vertical retrace. If the system supports it, you may
    // specify -1 to allow late swaps to happen immediately
    // instead of waiting for the next retrace.
    int swap_interval;

    // Instead of using vsync, you can specify a desired framerate
    // that the application will attempt to keep. If a frame rendered
    // too fast, it will sleep the remaining time. Leave swap_interval
    // at 0 when using this.
    int fps_lock;
};

struct Input
{
    struct Key
    {
        // These can be checked conveniently using the macro below,
        // given that you call it in a function which takes Input
        // in as a variable named io.
        bool down[SDL_NUM_SCANCODES];
        bool released[SDL_NUM_SCANCODES];
    } key;
    struct Mouse
    {
        vec2 pos; // Position in pixels [0, 0] at top-left window corner
        vec2 ndc; // Position in pixels mapped from [0, w]x[0, h] -> [-1, +1]x[+1, -1]
        vec2 rel; // Movement since last mouse event
        struct Button
        {
            bool down;
            bool released;
        } left, right, middle;
        struct Wheel
        {
            r32 x; // The amount scrolled horizontally
            r32 y; // The amount scrolled vertically
        } wheel;
    } mouse;
};

u08 *so_read_file_and_null_terminate(char *filename)
{
    SDL_RWops *rw = SDL_RWFromFile(filename, "rb");
    if (!rw)
    {
        // Failed to open file
        assert(false);
        return NULL;
    }

    s64 size_in_bytes = SDL_RWsize(rw);

    // Allocate + 1 for null terminator
    u08 *buffer = (u08*)malloc(size_in_bytes + 1);

    s64 bytes_read = 0;
    while (bytes_read < size_in_bytes)
    {
        u08 *position = buffer + bytes_read;
        s64 remaining = size_in_bytes - bytes_read;
        bytes_read += SDL_RWread(rw, position, 1, remaining);
    }
    SDL_RWclose(rw);
    if (bytes_read != size_in_bytes)
    {
        // Failed to read entire file
        assert(false);
        return NULL;
    }
    buffer[bytes_read] = 0;
    return buffer;
}

void so_free(u08 *data)
{
    free(data);
}
