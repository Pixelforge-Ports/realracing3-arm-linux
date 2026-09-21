/*
 * Real Racing 3 (Android 2.7.0) loader bootstrap.
 *
 * The original Java activity owns the SDL-equivalent window and calls the
 * exported JNI lifecycle functions once per frame. This loader reproduces
 * that small Java/GL shell; the original game library and data are supplied
 * by the player and are never distributed with the port.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include "display_config.h"

#include "platform.h"
#include "so_util.h"
#include "khronos/gles2.h"

#include "jni.h"
#include "classes/rr3_MainActivity.h"
#include "classes/cloudcell_defer.h"

#include "app_exit.h"
#include "crash.h"
#include "fix_path.h"
#include "gl_probe.h"
#include "port_version.h"
#include "sdl_info.h"
#include "trace.h"
#include "rr3_asset_patch.h"
#include "rr3_control_scheme.h"
#include "rr3_fmod_pump.h"
#include "rr3_savefile_patch.h"
#include "rr3_texture_guard.h"
#include "rr3_tutorial_trace.h"
#include "rr3_control.h"
#include "input_bridge.h"
#include "gl_stats.h"
#include "fb_probe.h"

/*
 * The OpenSL buffer queue's fallback tick.
 *
 * The mixer normally runs on its own thread, and then this does nothing. It
 * exists because the queue only refills from a callback, and something has to
 * make that callback happen: with no tick at all the game enqueues four
 * buffers and then goes silent for the rest of the run, which is how this port
 * shipped its first playable build. Driving it from the frame works but ties
 * the audio to the frame rate - see android/opensles.cpp for why that is
 * audible here and was not on the port this shim came from.
 */
extern "C" unsigned long bionic_mutex_lock_count(void);
extern "C" void android_opensles_frame_tick(void);
extern "C" void android_opensles_report(void);

/*
 * A running breakdown of the frame, printed rarely enough to be free.
 *
 * Averages over a window rather than reporting each frame: one slow frame says
 * nothing, and a per-frame line would add to the very cost being measured.
 */
static void frame_budget_account(uint32_t render_ms, uint32_t swap_ms,
                                 uint32_t audio_ms)
{
    static uint32_t n = 0, render = 0, swap = 0, audio = 0;
    static uint32_t worst_render = 0, worst_swap = 0;

    render += render_ms; swap += swap_ms; audio += audio_ms;
    if (render_ms > worst_render) worst_render = render_ms;
    if (swap_ms   > worst_swap)   worst_swap   = swap_ms;

    if (++n < 120)
        return;

    trace("frame budget over %u frames: render %.1f ms (worst %u), swap %.1f ms "
          "(worst %u), audio %.1f ms -> %.1f ms/frame = %.1f fps",
          n, (double)render / n, worst_render, (double)swap / n, worst_swap,
          (double)audio / n, (double)(render + swap + audio) / n,
          (render + swap + audio) ? 1000.0 * n / (double)(render + swap + audio)
                                  : 0.0);

    n = render = swap = audio = 0;
    worst_render = worst_swap = 0;
}

extern "C" long android_gl_draw_calls(void);
extern "C" long android_gl_textures_uploaded(void);
/* The two ways a file becomes content here: through the libc thunks
 * (src/symtab_io.cpp, which is how this game reads almost everything) and
 * through AAssetManager (android/asset_manager.cpp, the APK path). Summed
 * rather than picked - "how many assets did the run open" has no useful answer
 * that leaves one of them out. */
extern "C" long android_io_assets_opened(void);
extern "C" long android_assets_opened(void);
extern "C" int android_gl_shaders_compiled(void);
extern "C" int android_gl_shaders_failed(void);
extern "C" int android_gl_programs_linked(void);
extern "C" int android_gl_programs_failed(void);

/* DIAGNOSTIC (temporary, 3D-scene investigation) - see symtab_glprobe.cpp. */
extern "C" long android_gl_draw_arrays(void);
extern "C" long android_gl_draw_arrays_max(void);
extern "C" long android_gl_draw_elements(void);
extern "C" long android_gl_draw_elements_indices(void);
extern "C" long android_gl_draw_elements_max(void);

static const char *kNativeLib = "libRealRacing3.so";
static const char *kNativeLibDir = "lib/armeabi-v7a";
static int kWidth = 640;
static int kHeight = 480;
static const int kLandscape = 2;
static const int kRotation0 = 0;

static so_module *g_game_module = NULL;

/* Kept as a function because the inherited pthread diagnostics ask for the
 * current module. This is generic loader plumbing, not a game-specific patch. */
so_module *realracing3_module(void) { return g_game_module; }

extern "C" void android_egl_init(SDL_Window *window, SDL_GLContext gl);
extern "C" void viewport_scale_init(int physical_width, int physical_height);

static int report_unresolved_symbols(so_module *mod)
{
    int missing = 0;

    for (int i = 0; i < mod->num_dynsym; ++i) {
        Elf_Sym *sym = &mod->dynsym[i];
        if (sym->st_shndx != SHN_UNDEF)
            continue;

        const char *name = mod->dynstr + sym->st_name;
        if (!name || !*name || so_resolve_link(mod, name))
            continue;

        if (ELF32_ST_BIND(sym->st_info) == STB_WEAK) {
            trace("weak import left null: %s", name);
            continue;
        }

        fprintf(stderr, "unresolved symbol: %s\n", name);
        ++missing;
    }

    return missing;
}

extern "C" int so_after_relocate(so_module *mod)
{
    const char *name = mod->soname ? mod->soname : "<no DT_SONAME>";
    trace("module relocated: %s", name);

    int missing = report_unresolved_symbols(mod);
    if (missing) {
        fatal("%d import(s) of %s have no implementation", missing, name);
        return 1;
    }

    if (so_symbol(mod,
            "Java_com_firemint_realracing3_MainActivity_onCreateJNI")) {
        g_game_module = mod;
        crash_report_init(mod, kNativeLib);
        trace("primary game module identified: %s", name);
        /* Last point at which the text is writable and none of the game's own
         * code has run, which is what the SkipAsset hook needs. */
        rr3_apply_asset_patches(mod);
        rr3_apply_savefile_patch(mod);
        rr3_install_tutorial_trace(mod);
    }

    return 0;
}

/*
 * Everything worth knowing about a failed window or context, in one place.
 *
 * "Can't load EGL/GL library on window creation" is the only thing SDL says,
 * and it says it for two unrelated causes: the EGL library could not be
 * dlopen()ed at all, or it loaded and its initialisation failed. A field log
 * carrying just that sentence cannot be acted on.
 *
 * This port makes those failures fatal - without a context it has nothing to
 * render into - so the forensics run on the way out, which is also the only
 * place they cost nothing: a successful boot prints none of this.
 *
 * What is printed is the state SDL decided from: which video driver is live,
 * which ones were compiled in, the four environment variables that steer the
 * GL search as the process actually sees them, then the EGL library SDL would
 * have used, walked step by step and audited for missing dependencies.
 */
static void trace_probe_line(void *ctx, const char *line)
{
    (void)ctx;
    trace("  %s", line);
}

static void log_window_failure_forensics(const char *stage)
{
    trace("window failure forensics (%s)", stage);
    trace("  SDL_GetError: %s", SDL_GetError());

    const char *current = SDL_GetCurrentVideoDriver();
    trace("  current video driver: %s", current ? current : "(none initialised)");

    char drivers[256];
    size_t used = 0;
    int count = SDL_GetNumVideoDrivers();
    for (int i = 0; i < count && used + 1 < sizeof(drivers); i++) {
        const char *name = SDL_GetVideoDriver(i);
        int written = snprintf(drivers + used, sizeof(drivers) - used, "%s%s",
                               used ? " " : "", name ? name : "?");
        if (written < 0)
            break;
        used += (size_t)written;
    }
    trace("  compiled-in video drivers (%d): %s", count, used ? drivers : "(none)");

    /*
     * As the process sees them, not as the launcher set them: an unset variable
     * here and a set one there is the difference between "SDL never looked at
     * our shim" and "it looked and the shim is wrong".
     */
    static const char *const kGlEnv[] = {
        "SDL_VIDEO_EGL_DRIVER", "SDL_VIDEO_GL_DRIVER",
        "SDL_VIDEODRIVER", "LD_LIBRARY_PATH",
    };
    for (size_t i = 0; i < sizeof(kGlEnv) / sizeof(kGlEnv[0]); i++) {
        const char *value = getenv(kGlEnv[i]);
        trace("  %s=%s", kGlEnv[i], value ? value : "(unset)");
    }

    /*
     * SDL's own default when SDL_VIDEO_EGL_DRIVER is unset. Walking it here,
     * in the process that just failed and with the linker state SDL used, is
     * what separates a dlopen-level failure from an EGL-init-level one - and
     * the dependency audit turns "something is missing" into the list.
     */
    const char *egl = getenv("SDL_VIDEO_EGL_DRIVER");
    if (!egl || !*egl)
        egl = "libEGL.so.1";

    gl_probe_init(egl, trace_probe_line, NULL);
    gl_probe_deps(egl, trace_probe_line, NULL);
}

template <typename T>
static T required_symbol(so_module *mod, const char *name)
{
    T symbol = reinterpret_cast<T>(so_symbol(mod, name));
    if (!symbol)
        fatal("required JNI entry point is missing: %s", name);
    return symbol;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /*
     * The launcher's GL provider preflight, before anything that expects a
     * game directory: these modes load one library and exit.
     */
    if (argc >= 2 && strcmp(argv[1], "--gl-probe") == 0)
        return gl_probe_main(argc - 2, argv + 2);
    if (argc >= 3 && strcmp(argv[1], "--gl-probe-init") == 0)
        return gl_probe_init(argv[2], gl_probe_report_stdout, NULL);
    if (argc >= 3 && strcmp(argv[1], "--gl-probe-deps") == 0)
        return gl_probe_deps(argv[2], gl_probe_report_stdout, NULL);

    /*
     * The same idea one layer up: the launcher has to pick a video backend for
     * SDL, and only SDL knows which ones it was built with. No SDL_Init here -
     * see src/sdl_info.h.
     */
    if (argc >= 2 && strcmp(argv[1], "--sdl-info") == 0)
        return sdl_info_main();

    /*
     * The launcher asks the binary for the version rather than carrying its own
     * copy, so the two can never disagree. Plain stdout, not trace(): the caller
     * is a shell substitution, and it runs before LOADER_TRACE means anything.
     */
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("%s\n", REALRACING3_PORT_VERSION);
        return 0;
    }

    /* First line of every run: a log that does not name its build cannot be
     * told apart from a log produced by the release before it. */
    trace("Real Racing 3 port v%s", REALRACING3_PORT_VERSION);

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <realracing3-directory>\n\n"
                "Expected: lib/armeabi-v7a/libRealRacing3.so plus the data\n"
                "from your own legal copy of Real Racing 3 2.7.0.\n",
                argv[0]);
        return 2;
    }

    char resolved_game_dir[PATH_MAX];
    if (!realpath(argv[1], resolved_game_dir)) {
        fatal("could not resolve game directory '%s'", argv[1]);
        return 1;
    }
    const char *game_dir = resolved_game_dir;
    io_set_game_dir(game_dir);

    /* Before anything can open profile.dat: the device has a gamepad and no
     * accelerometer, so it must not boot into the Android tilt default. */
    rr3_seed_control_scheme(game_dir);

    char lib_dir[PATH_MAX];
    char lib_path[PATH_MAX];
    snprintf(lib_dir, sizeof(lib_dir), "%s/%s", game_dir, kNativeLibDir);
    snprintf(lib_path, sizeof(lib_path), "%s/%s", lib_dir, kNativeLib);

    struct stat st;
    if (stat(lib_path, &st) != 0) {
        fatal("missing '%s' (the current bootstrap targets Android 2.7.0)",
              lib_path);
        return 1;
    }
    trace("native library found: %s (%lld bytes)",
          lib_path, (long long)st.st_size);

    /* RR3's native VFS resolves its downloaded content through relative
     * names such as eds/AndroidHigh.plist and dataoffsets.txt. Android starts
     * the process in its content root; reproduce that before constructors or
     * lifecycle callbacks can open a file. */
    if (chdir(game_dir) != 0) {
        fatal("could not enter game directory '%s'", game_dir);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        fatal("SDL_Init failed: %s", SDL_GetError());
        /* Naming a backend SDL was not built with is one of the two ways to get
         * here, and the launcher can set SDL_VIDEODRIVER. The driver list and
         * the env block say immediately which of the two this is. */
        log_window_failure_forensics("SDL_Init");
        return 1;
    }

    if (!display_config::detect("REALRACING3", kWidth, kHeight, false)) return 2;
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_Window *window = SDL_CreateWindow(
        "Real Racing 3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) {
        fatal("SDL_CreateWindow failed: %s", SDL_GetError());
        log_window_failure_forensics("GLES 2.0 window");
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        fatal("could not create an OpenGL ES 2.0 context: %s", SDL_GetError());
        log_window_failure_forensics("GLES 2.0 context");
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl);

    /*
     * No vsync by default, which is what the four sibling ports on this
     * console do - none of them calls SetSwapInterval at all, and SDL starts
     * at 0. This port asked for 1, and that only helps a game that can hold
     * the refresh rate. This one renders a frame in about 48 ms, so waiting
     * for the scanout adds latency and rounds every frame up to the next
     * 16.7 ms boundary, which is measured time spent doing nothing.
     *
     * Set REALRACING3_VSYNC=1 to put it back: without it there can be tearing,
     * and on a device that does hit 60 that trade goes the other way.
     */
    const char *vsync = getenv("REALRACING3_VSYNC");
    const int   want_vsync = (vsync && *vsync && *vsync != '0') ? 1 : 0;
    if (SDL_GL_SetSwapInterval(want_vsync) != 0 && want_vsync)
        trace("vsync requested but refused by the driver: %s", SDL_GetError());
    trace("vsync: %s", want_vsync ? "on" : "off (set REALRACING3_VSYNC=1 to enable)");

    load_gles1_funcs();
    load_gles2_funcs();
    android_egl_init(window, gl);

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        if (!display_config::drawable("REALRACING3", drawable_width, drawable_height, kWidth, kHeight, false)) return 2;
    viewport_scale_init(drawable_width, drawable_height);
    /* After both GL tables are filled: the dispatch audit reads them. */
    gl_stats_init();
    trace("GLES2 surface ready: %dx%d", drawable_width, drawable_height);

    JavaVM *vm = NULL;
    JNIEnv *env = NULL;
    if (JNI_CreateJavaVM(&vm, &env, NULL) != JNI_OK || !vm || !env) {
        fatal("could not create the fake Java VM");
        return 1;
    }

    /* lib_dir is absolute and remains valid after chdir. */
    so_set_options(NULL, lib_dir);
    so_module *mod = so_load_module(kNativeLib, NULL, NULL);
    if (!mod || !g_game_module) {
        fatal("could not load %s from %s", kNativeLib, lib_dir);
        return 1;
    }
    trace("game library initialized at %p", (void *)mod->text_base);
    /* Before the engine loads its first texture: a descrambler failure
     * must become a missing texture, not a null dereference. */
    rr3_texture_guard_init(mod);
    android_input_init(mod, env, kWidth, kHeight);

    typedef void (ABI_ATTR *ActivityFn)(JNIEnv *, jobject);
    typedef void (ABI_ATTR *SurfaceChangedFn)(
        JNIEnv *, jobject, jint, jint, jint, jint);
    typedef void (ABI_ATTR *RenderFn)(JNIEnv *, jobject, jint, jint);
    typedef void (ABI_ATTR *FocusFn)(JNIEnv *, jobject, jboolean);

    ActivityFn on_create = required_symbol<ActivityFn>(mod,
        "Java_com_firemint_realracing3_MainActivity_onCreateJNI");
    ActivityFn on_start = required_symbol<ActivityFn>(mod,
        "Java_com_firemint_realracing3_MainActivity_onStartJNI");
    ActivityFn on_resume = required_symbol<ActivityFn>(mod,
        "Java_com_firemint_realracing3_MainActivity_onResumeJNI");
    ActivityFn on_view_created = required_symbol<ActivityFn>(mod,
        "Java_com_firemint_realracing3_MainActivity_onViewCreatedJNI");
    SurfaceChangedFn on_view_changed = required_symbol<SurfaceChangedFn>(mod,
        "Java_com_firemint_realracing3_MainActivity_onViewChangedJNI");
    RenderFn on_view_render = required_symbol<RenderFn>(mod,
        "Java_com_firemint_realracing3_MainActivity_onViewRenderJNI");
    FocusFn on_focus = reinterpret_cast<FocusFn>(so_symbol(mod,
        "Java_com_firemint_realracing3_MainActivity_onWindowFocusChangedJNI"));

    if (!on_create || !on_start || !on_resume || !on_view_created ||
        !on_view_changed || !on_view_render)
        return 1;

    RR3MainActivity activity;
    jobject activity_object = reinterpret_cast<jobject>(&activity);

    trace("-> MainActivity.onCreateJNI");
    on_create(env, activity_object);
    trace("<- MainActivity.onCreateJNI");
    on_start(env, activity_object);
    trace("MainActivity.onStartJNI returned");
    on_resume(env, activity_object);
    trace("MainActivity.onResumeJNI returned");
    on_view_created(env, activity_object);
    trace("MainActivity.onViewCreatedJNI returned");
    on_view_changed(env, activity_object, kWidth, kHeight,
                    kLandscape, kRotation0);
    trace("MainActivity.onViewChangedJNI returned (%dx%d)", kWidth, kHeight);
    if (on_focus)
        on_focus(env, activity_object, JNI_TRUE);

    rr3_control_init(getenv("REALRACING3_CONTROL_DIR"));

    /* On Android the game's AudioStreamManager starts FMOD's mixer thread from
     * Java. Nothing here can, so the port runs it instead - started after the
     * activity is up, and patient about the rest: it waits for FMOD to publish
     * a format rather than assuming the engine is ready by now. */
    rr3_fmod_pump_start(env);

    const char *limit_string = getenv("REALRACING3_FRAME_LIMIT");
    const bool skip_render = getenv("REALRACING3_SKIP_RENDER") &&
                             strcmp(getenv("REALRACING3_SKIP_RENDER"), "1") == 0;
    const long frame_limit = limit_string ? atol(limit_string) : 0;
    long frames = 0;
    bool running = true;

    while (running && (frame_limit <= 0 || frames < frame_limit)) {
        if (!rr3_control_tick(frames))
            break;
        /* Every event goes to the input bridge, which is what turns a physical
         * button into a ControllerManager ordinal and a stick into an axis
         * value. Without this the loop drained SDL's queue and threw the
         * events away: on the first hardware run SDL opened the pad
         * ("controller: GO-Super Gamepad", joysticks=1 controllers=1) and the
         * game still answered to nothing, because no press ever reached
         * android_input_event(). It went unnoticed for the whole project
         * because the emulator drives input through the MCP channel
         * (android_input_inject_*), which enters the bridge directly and never
         * touches SDL - so the path a real player uses was the one path never
         * exercised. android_input_event() returns false on SDL_QUIT. */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!android_input_event(&event))
                running = false;
        }
        if (!running)
            break;

        /*
         * The game's own exit request, checked next to the event drain rather
         * than trusted to it: android_app_request_exit() pushes SDL_QUIT, but
         * a full queue drops it, and a dropped exit is the freeze all over
         * again. Read before the render call, so the frame after finish() is
         * never issued against a world the engine has already released.
         */
        if (android_app_exit_requested()) {
            running = false;
            break;
        }

        /* Deliver held MCP/controller sticks once per game frame, matching
         * the Android poll cadence used by the native input bridge. */
        android_input_tick();

        /* Synthetic input, only under REALRACING3_AUTOPILOT. Injected here
         * rather than after the render so the tap is in the engine's queue
         * before the frame that would act on it. */
        android_input_autopilot_tick(frames);

        /* Answer everything the Cloudcell facades queued last frame, from the
         * game thread and outside every Cloudcell lock. Before the render call,
         * so the same frame's CC_HttpRequestManager_Class::Update() drains the
         * completions it produces - see jni/classes/cloudcell_defer.h. */
        cloudcell_pump();

        /*
         * Where the frame actually goes.
         *
         * The console runs this at about 12 fps - 81 ms a frame - and nothing
         * in the log says which part of the frame owns that time. The GL
         * census rules out the obvious suspect: 128 draw calls and ~14k
         * triangles at 640x480 is not a lot to ask of a Mali-G31. And the
         * per-line trace, whatever it costs, cannot cost tens of milliseconds.
         *
         * Three timers answer it. Rendering is the game's own code plus our GL
         * thunks; the swap is vsync or the GPU; the pump is FMOD mixing, which
         * runs on this thread and grows with the audio buffer. Guessing which
         * of the three to attack has already been wrong twice.
         */
        const uint32_t t_frame_start = SDL_GetTicks();

        if (!skip_render) {
            trace("-> MainActivity.onViewRenderJNI #%ld", frames + 1);
            on_view_render(env, activity_object, kLandscape, kRotation0);
            trace("<- MainActivity.onViewRenderJNI #%ld returned", frames + 1);
        }
        const uint32_t t_after_render = SDL_GetTicks();

        /*
         * Both of these read the back buffer, so they have to run before the
         * swap: after it the contents are undefined.
         *
         * The probe was already wired into android/egl_shim.cpp's
         * eglSwapBuffers, and that call site never fired once - this game
         * presents through the loader's own SDL swap and never calls
         * eglSwapBuffers at all, so "TRACE: framebuffer non-black" had never
         * appeared in a run log. The probe stops reading as soon as it has
         * seen a drawn frame, so the cost is paid only while the answer is
         * still "black". The autopilot sampler is its own kind of cheap: one
         * row, every fifteenth frame, and only under the env var.
         */
        {
            int fb_w = 0, fb_h = 0;
            SDL_GL_GetDrawableSize(window, &fb_w, &fb_h);
            android_fb_probe(frames + 1, fb_w, fb_h);
        }
        android_input_autopilot_sample(frames + 1);

        trace("-> SDL_GL_SwapWindow #%ld", frames + 1);
        SDL_GL_SwapWindow(window);
        trace("<- SDL_GL_SwapWindow #%ld returned", frames + 1);
        const uint32_t t_after_swap = SDL_GetTicks();

        /* A no-op once the mixer has its own thread, which it normally
         * does; see android_opensles_frame_tick(). */
        android_opensles_frame_tick();
        const uint32_t t_after_audio = SDL_GetTicks();

        frame_budget_account(t_after_render - t_frame_start,
                             t_after_swap   - t_after_render,
                             t_after_audio  - t_after_swap);

        trace("-> rr3_control_after_draw #%ld", frames + 1);
        rr3_control_after_draw(frames + 1, kWidth, kHeight);
        trace("<- rr3_control_after_draw #%ld returned", frames + 1);
        ++frames;
        gl_stats_frame(frames);
        if (frames == 1 || frames == 60 || frames == 300 || frames % 200 == 0)
            trace("GL stats at frame %ld: draws=%ld textures=%ld shaders=%d/%d programs=%d/%d",
                  frames, android_gl_draw_calls(), android_gl_textures_uploaded(),
                  android_gl_shaders_compiled(), android_gl_shaders_failed(),
                  android_gl_programs_linked(), android_gl_programs_failed());
        /* DIAGNOSTIC (temporary): a race frame that draws the world submits
         * indexed geometry; a frame that only draws the HUD does not. */
        if (frames % 200 == 0)
            trace("GL draws at frame %ld: arrays=%ld (max count=%ld) "
                  "elements=%ld (indices=%ld, max count=%ld)",
                  frames, android_gl_draw_arrays(), android_gl_draw_arrays_max(),
                  android_gl_draw_elements(), android_gl_draw_elements_indices(),
                  android_gl_draw_elements_max());
        if (frames <= 5 || frames % 60 == 0)
            trace("frames=%ld", frames);
    }

    trace("run finished: %ld frame(s)", frames);
    /*
     * The run in three numbers, printed unconditionally at the end.
     *
     * "It survived N frames" is the claim a port can make while doing nothing:
     * a loop calling into an engine that draws a cleared screen ticks its
     * counter just as happily as one running a game. Assets opened, textures
     * uploaded and draws issued are the three that cannot be faked by
     * surviving - they only move when the engine is loading its own content
     * and putting it on the GPU. The harness reads this line for M6.
     */
    trace("summary assets=%ld textures=%ld draws=%ld",
          android_io_assets_opened() + android_assets_opened(),
          android_gl_textures_uploaded(), android_gl_draw_calls());
    /* Zeroes unless REALRACING3_AUTOPILOT drove the run; M7 reads this. */
    trace("autopilot keys=%ld scenes=%ld",
          android_input_autopilot_keys(), android_input_autopilot_scenes());
    rr3_report_asset_patch_stats();
    /* Both of these were written and then never called, so their counters were
     * only ever visible to a debugger. They are the two "how bad was it"
     * numbers of the run: art that silently did not decode, and audio the
     * speaker did not get in time. */
    rr3_texture_guard_report();
    android_opensles_report();
    /* The guest's mutex traffic. EAThread emulates 64-bit atomics with a table
     * of 32 mutexes locked on every 64-bit load and store, so this is the one
     * loader path whose volume could plausibly account for milliseconds of a
     * frame. Nobody had ever counted it; per-frame is the number that matters,
     * not the total. */
    if (frames > 0)
        trace("guest mutex locks: %lu total, %lu per frame",
              bionic_mutex_lock_count(), bionic_mutex_lock_count() / (unsigned long)frames);
    rr3_control_shutdown(frames);
    fflush(NULL);
    _exit(0);
}
