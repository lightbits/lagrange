#include "platform.h"
#include "lib/imgui/imgui_draw.cpp"
#include "lib/imgui/imgui.cpp"
#include "lib/imgui/imgui_demo.cpp"
#include "lib/imgui/imgui_impl_sdl.cpp"

void crashv(const char *fmt, va_list args)
{
    static char text[4096];
    int written = vsnprintf(text, sizeof(text), fmt, args);
    text[written-1] = 0;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Catastrophic failure", text, 0);
}

void crash(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    crashv(fmt, args);
    va_end(args);
    exit(1);
}

const char *gl_error_message(GLenum error)
{
    switch (error)
    {
    case 0: return "NO_ERROR";
    case 0x0500: return "INVALID_ENUM";
    case 0x0501: return "INVALID_VALUE";
    case 0x0502: return "INVALID_OPERATION";
    case 0x0503: return "STACK_OVERFLOW";
    case 0x0504: return "STACK_UNDERFLOW";
    case 0x0505: return "OUT_OF_MEMORY";
    case 0x0506: return "INVALID_FRAMEBUFFER_OPERATION";
    default: return "UNKNOWN";
    }
}

u64 perf_counter()
{
    return SDL_GetPerformanceCounter();
}

r32 perf_seconds(u64 begin, u64 end)
{
    u64 frequency = SDL_GetPerformanceFrequency();
    return (r32)(end - begin) / (r32)frequency;
}

r32 time_since(u64 then)
{
    u64 now = perf_counter();
    return perf_seconds(then, now);
}

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    {
        crash("Failed to initialize SDL: %s", SDL_GetError());
    }

    VideoMode mode = {};
    mode.width = 800;
    mode.height = 600;
    mode.gl_major = 1;
    mode.gl_minor = 5;
    mode.double_buffer = 1;
    mode.depth_bits = 24;
    mode.stencil_bits = 8;
    mode.multisamples = 4;
    mode.swap_interval = 1;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, mode.gl_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, mode.gl_minor);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,          mode.double_buffer);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,            mode.depth_bits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,          mode.stencil_bits);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,    mode.multisamples>0?1:0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,    mode.multisamples);

    SDL_Window *window = SDL_CreateWindow(
        "IARC",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        mode.width, mode.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!window)
    {
        crash("Failed to create a window: %s", SDL_GetError());
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(mode.swap_interval);

    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &mode.gl_major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &mode.gl_minor);
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER,          &mode.double_buffer);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE,            &mode.depth_bits);
    SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE,          &mode.stencil_bits);
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES,    &mode.multisamples);
    mode.swap_interval = SDL_GL_GetSwapInterval();

    ImGui_ImplSdl_Init(window);
    game_init();

    Input input = {};

    bool running = true;
    u64 initial_tick = perf_counter();
    u64 last_frame_t = initial_tick;
    r32 elapsed_time = 0.0f;
    r32 delta_time = 1.0f / 60.0f;
    while (running)
    {
        for (u32 i = 0; i < SDL_NUM_SCANCODES; i++)
            input.key.released[i] = false;
        input.mouse.left.released = false;
        input.mouse.right.released = false;
        input.mouse.middle.released = false;
        input.mouse.wheel.x = 0.0f;
        input.mouse.wheel.y = 0.0f;
        input.mouse.rel.x = 0.0f;
        input.mouse.rel.y = 0.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSdl_ProcessEvent(&event);
            switch (event.type)
            {
                case SDL_KEYDOWN:
                {
                    input.key.down[event.key.keysym.scancode] = true;
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        running = false;
                    break;
                }

                case SDL_KEYUP:
                {
                    input.key.down[event.key.keysym.scancode] = false;
                    input.key.released[event.key.keysym.scancode] = true;
                    break;
                }

                case SDL_MOUSEMOTION:
                {
                    input.mouse.pos.x = event.motion.x;
                    input.mouse.pos.y = event.motion.y;
                    input.mouse.rel.x = event.motion.xrel;
                    input.mouse.rel.y = event.motion.yrel;
                    break;
                }

                case SDL_MOUSEBUTTONDOWN:
                {
                    if (SDL_BUTTON_LMASK & event.button.button)
                        input.mouse.left.down = true;
                    if (SDL_BUTTON_RMASK & event.button.button)
                        input.mouse.right.down = true;
                    if (SDL_BUTTON_MMASK & event.button.button)
                        input.mouse.middle.down = true;
                    break;
                }

                case SDL_MOUSEBUTTONUP:
                {
                    if (SDL_BUTTON_LMASK & event.button.button)
                    {
                        input.mouse.left.down = false;
                        input.mouse.left.released = true;
                    }
                    if (SDL_BUTTON_RMASK & event.button.button)
                    {
                        input.mouse.right.down = false;
                        input.mouse.right.released = true;
                    }
                    if (SDL_BUTTON_MMASK & event.button.button)
                    {
                        input.mouse.middle.down = false;
                        input.mouse.middle.released = true;
                    }
                    break;
                }

                case SDL_MOUSEWHEEL:
                {
                    input.mouse.wheel.x = event.wheel.x;
                    input.mouse.wheel.y = event.wheel.y;
                    break;
                }

                case SDL_WINDOWEVENT:
                {
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    {
                        mode.width = event.window.data1;
                        mode.height = event.window.data2;
                    }
                } break;

                case SDL_QUIT:
                {
                    running = false;
                } break;
            }
        }
        input.mouse.ndc.x = -1.0f + 2.0f * input.mouse.pos.x / (r32)mode.width;
        input.mouse.ndc.y = +1.0f - 2.0f * input.mouse.pos.y / (r32)mode.height;
        ImGui_ImplSdl_NewFrame(window);
        game_tick(input, mode, elapsed_time, 1.0f/60.0f);
        ImGui::Render();
        SDL_GL_SwapWindow(window);

        delta_time = time_since(last_frame_t);
        if (mode.fps_lock > 0)
        {
            r32 target_time = 1.0f / (r32)mode.fps_lock;
            r32 sleep_time = target_time - delta_time;
            if (sleep_time >= 0.01f)
                SDL_Delay((u32)(sleep_time * 1000.0f));
            delta_time = time_since(last_frame_t);
        }
        last_frame_t = perf_counter();
        elapsed_time = time_since(initial_tick);

        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            crash("An error occurred: %s", gl_error_message(error));
        }
    }

    ImGui_ImplSdl_Shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
