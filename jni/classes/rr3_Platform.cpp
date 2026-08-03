#include <stdio.h>
#include <new>

#include "platform.h"
#include "jni.h"
#include "jni_internals.h"
#include "cloudcell_defer.h"
#include "cloudcell_natives.h"
#include "trace.h"

/*
 * com.firemint.realracing3.Platform - the class that answers RR3's native
 * alert dialogs, and with them the hard stall on the load bar.
 *
 * Measured chain, from the 03:23 run (emulator.log, 122 MB, QEMU_STRACE):
 *
 *   AssetDownloadService::OnUpdate (0x3276b8)
 *     -> GetFirstWifiNetworkCheckFailed (0x325098): isNetworkAvailable(true)
 *        is false and GetQueuedDownloadSize(true) is not zero
 *     -> ShowNoWifiMessage (0x323534): writes CC_AssetManager+0x8d = 1
 *        (0x3235a8 / 0x323610) and calls system_ShowPlatformMessageWithButtons
 *        -> ndPlatformJNI::addAlertMessage -> Platform.showMessage
 *   and OnUpdate's first instruction is `if (am[0x8d]) return;` (0x3276e0).
 *
 * The only writers of that byte back to 0 in the entire binary are
 * CancelAllDownloads (0x31ffe0), ContinueDownloadingVia3G (0x31ff1c) and
 * OnResume - i.e. the dialog's own button handlers. With no Platform class the
 * dialog could never be shown, so no button could ever be pressed, so
 * AssetDownloadService::OnUpdate returned at its first instruction for the
 * remaining 850 000 frames of the run. That is the freeze.
 *
 * showMessage below closes that loop. src/rr3_asset_patch.cpp removes the
 * reason the dialog is raised in the first place; this class is the safety net
 * for every other alert the engine can raise, of which there are many.
 */
class RR3Platform : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }
};

/* Java: public native void alertMessageExecuteCallback(int) on MainActivity.
 * Verified at 0x8b37f0: the export reads only r2 (the jint) before tail-calling
 * ndActivity::alertMessageExecuteCallback, so env/thiz are unused - but they
 * are passed properly anyway. */
using AlertExecuteFn = void (*)(JNIEnv *, jobject, jint);

/*
 * Answer with the positive button, always.
 *
 * MainActivity.Message (MainActivity.java:205) maps the three buttons to:
 *   positive -> alertMessageExecuteCallback(idPositive)
 *   neutral  -> alertMessageExecuteCallback(idNeutral)
 *   negative -> Platform.exitApp()
 * so the negative one must never be fired - it quits the game.
 *
 * For the no-Wifi dialog specifically, ShowNoWifiMessage's call at 0x32367c
 * passes a real function pointer only for the positive slot (the negative and
 * neutral callback words are both 0 on the path this port takes, because
 * isMobileDataAvailable() answers false). So there is exactly one button that
 * does anything, and it is the positive one.
 *
 * Deferred, like everything else Cloudcell: on Android the button press arrives
 * from the UI thread through GLView.queueEvent, never inside the call that
 * raised the dialog. Answering inline would run CancelAllDownloads in the
 * middle of ShowNoWifiMessage.
 *
 * The whole tuple is traced because the id -> handler mapping is assigned at
 * runtime by ndPlatformJNI::addAlertMessage (0x8b7994), which hands out
 * incrementing ids from a global counter; the log is the only way to see which
 * dialog was raised and which id it wanted.
 */
static void platform_show_message(JNIEnv *env, jclass, jstring title, jstring body,
                                  jstring positive, jstring negative, jstring neutral,
                                  jint id_positive, jint id_negative, jint id_neutral)
{
    auto text = [](jstring s) -> const char * {
        String *str = (String *)s;
        return (str && str->str) ? str->str : "";
    };

    trace("Platform.showMessage: title='%s' body='%s' pos='%s'/%d neg='%s'/%d "
          "neutral='%s'/%d -> answering positive",
          text(title), text(body), text(positive), (int)id_positive,
          text(negative), (int)id_negative, text(neutral), (int)id_neutral);

    cloudcell_defer([env, id_positive]() {
        static AlertExecuteFn fn = cloudcell_native<AlertExecuteFn>(
            "Java_com_firemint_realracing3_MainActivity_alertMessageExecuteCallback");
        if (!fn) {
            warning("Platform.showMessage: alertMessageExecuteCallback export "
                    "missing; the dialog stays unanswered\n");
            return;
        }
        fn(env, NULL, id_positive);
    });
}

/*
 * Network state. All three answer "offline", which is the truth on this
 * console and is also what keeps CC_Helpers::IsConnectedToInternet from
 * starting work that cannot finish.
 *
 * isNetworkSettingsShown() must stay false: Java sets that flag in
 * openNetworkSettings() and clears it when the settings activity returns, and
 * the engine polls it to decide whether it is still waiting for the user.
 */
static jboolean platform_is_network_available(JNIEnv *, jclass, jboolean) { return JNI_FALSE; }
static jboolean platform_is_mobile_data_available(JNIEnv *, jclass) { return JNI_FALSE; }
static jboolean platform_is_network_settings_shown(JNIEnv *, jclass) { return JNI_FALSE; }
static void platform_open_network_settings(JNIEnv *, jclass) {}

/* Locale.getDefault().toString(). The donor ships text_en.txt, so this is the
 * language file that will actually resolve. */
static jstring platform_get_locale(JNIEnv *env, jclass) { return env->NewStringUTF("en_US"); }
/* TelephonyManager.getNetworkOperatorName() with no radio. */
static jstring platform_get_carrier(JNIEnv *env, jclass) { return env->NewStringUTF(""); }

/* Crashlytics is not linked into this port; the engine only ever writes to it. */
static void platform_set_crashlytics_int(JNIEnv *, jclass, jint, jstring) {}
static void platform_set_crashlytics_string(JNIEnv *, jclass, jstring, jstring) {}

/* deleteDir() on a directory that is not there returns true (File.delete on a
 * missing path is a no-op in the recursive walk). The engine uses this to clear
 * stale download staging directories. */
static jboolean platform_delete_directory(JNIEnv *, jclass, jstring) { return JNI_TRUE; }

/* /proc/meminfo MemTotal in MB. The R36S has 1 GB; the engine uses this only to
 * pick texture budgets, and reporting the real figure is better than 0, which
 * some of those comparisons treat as "unknown, assume the smallest tier". */
static jint platform_get_total_memory(JNIEnv *, jclass) { return 1024; }

/*
 * exitApp() is only reached from the negative button of an alert - which this
 * port never presses - and from the engine's own quit path. Traced and left as
 * a no-op: the loader owns the run loop (src/main.cpp) and tearing the process
 * down from inside a JNI call would lose the frame accounting the harness
 * checks. If a run ever shows this being hit, that is a finding, not noise.
 */
static void platform_exit_app(JNIEnv *, jclass)
{
    trace("Platform.exitApp called - ignored, the loader owns the run loop");
}

/*
 * The rest of ndPlatformJNI's surface.
 *
 * These only became visible once the class existed: with no Platform at all
 * every lookup died at FindClass, and the first run after registering it turned
 * 26 further "Class Platform does not have static method ..." lines up in the
 * log - the engine caching its whole method table at init. They are not new
 * failures, they are the same NULL ids the port has always had, now itemised.
 *
 * Device identity is answered consistently with the values
 * jni/classes/cloudcell_compat.cpp already reports through GetInfo, so the two
 * paths cannot disagree about what machine this is.
 */
static jstring platform_get_device_uid(JNIEnv *env, jclass) { return env->NewStringUTF("rr3-r36s-local"); }
static jstring platform_get_model_id(JNIEnv *env, jclass) { return env->NewStringUTF("R36S"); }
static jstring platform_get_model_name(JNIEnv *env, jclass) { return env->NewStringUTF("R36S"); }
static jstring platform_get_app_name(JNIEnv *env, jclass) { return env->NewStringUTF("Real Racing 3"); }
static jstring platform_get_app_version(JNIEnv *env, jclass) { return env->NewStringUTF("2.7.0"); }
/* Build.VERSION.RELEASE. 2.7 shipped for KitKat-era devices; nothing here
 * depends on the exact value beyond ">= the minimum the game supports". */
static jstring platform_get_os_version(JNIEnv *env, jclass) { return env->NewStringUTF("4.4.4"); }
static jint platform_get_api_level(JNIEnv *, jclass) { return 19; }

/*
 * getAppPath / getExternalStorageDir / getInternalStorageDir /
 * getExternalStorageState / extractRes are deliberately NOT registered.
 *
 * They were, for one run, all answering io_game_dir(). It regressed: the engine
 * uses those roots to build its Android search list, so every content lookup
 * moved from "/game/gametext.txt" - which exists - to "/game/.depot/…",
 * "/game/apk/res/…" and "/game/doc/…", which do not, and the run died with a
 * SIGSEGV in libRealRacing3.so+0x3c99fc after langs.utf8, gametext.txt and
 * text_.txt all failed to load. With the methods absent the engine keeps its
 * own default root and the port's file layer (src/symtab_io.cpp) resolves
 * everything, which is the arrangement that was already working.
 *
 * If they are ever needed, the answer is not io_game_dir() - it is whatever
 * makes "<answer>/apk/res" land on the extracted tree, which no single value of
 * these three can do at once.
 */

/* Java reads PackageInfo.firstInstallTime, in milliseconds. There is no install
 * record here; 0 is "unknown", which is what a fresh install also looks like. */
static jlong platform_get_app_install_time(JNIEnv *, jclass) { return 0; }

/*
 * Debug.getNativeHeapAllocatedSize()/getNativeHeapFreeSize(), in bytes.
 *
 * Deliberately optimistic: every consumer of these is a low-memory guard, and
 * the failure mode of a wrong answer is one-directional - report too little
 * free and the engine drops to a smaller texture tier or refuses content;
 * report plenty and it behaves as it would on the 1 GB device this is. The
 * numbers are a floor, not a measurement, and are marked as such here rather
 * than dressed up as one.
 */
static jlong platform_get_app_memory_free(JNIEnv *, jclass) { return 512LL * 1024 * 1024; }
static jlong platform_get_app_memory_usage(JNIEnv *, jclass) { return 128LL * 1024 * 1024; }
static void platform_memory_probe(JNIEnv *, jclass) {}

/* The surface this port actually creates - kWidth/kHeight in src/main.cpp -
 * with a density-1 panel and no rotation. */
static jint platform_get_screen_width(JNIEnv *, jclass) { return 640; }
static jint platform_get_screen_height(JNIEnv *, jclass) { return 480; }
static jint platform_get_screen_dpi(JNIEnv *, jclass) { return 160; }
static jint platform_get_screen_rotation(JNIEnv *, jclass) { return 0; }

/* No browser and no photo gallery on this console. saveImage answers false
 * rather than pretending, so the engine reports the failure to the player
 * instead of leaving them looking for a file that was never written. */
static void platform_open_url(JNIEnv *, jclass, jstring) {}
static jboolean platform_save_image(JNIEnv *, jclass, jintArray, jint, jint, jint, jstring)
{ return JNI_FALSE; }

/*
 * loadTextureFromMemory decodes a PNG/JPEG through android.graphics.Bitmap and
 * hands back a com.firemint.realracing3.TextureInfo. NULL, because that class
 * is not registered and because this port decodes textures itself
 * (src/dxt_decompress.cpp, src/atc_decompress.cpp, PVRTDecompress). If a run
 * ever shows this being called for real, registering TextureInfo is the fix -
 * not returning a fake object.
 */
static jobject platform_load_texture_from_memory(JNIEnv *, jclass, jbyteArray, jint)
{ return NULL; }

/* Keeps the screen awake on Android; nothing to keep awake here. */
static void platform_toggle_idle_mode(JNIEnv *, jclass, jboolean) {}
/* Literally `return i + 1` in the Java - a liveness check for the JNI bridge,
 * so it has to behave exactly. */
static jint platform_test_func(JNIEnv *, jclass, jint value) { return value + 1; }

/* The engine does build an instance of this otherwise-static class and keeps it
 * as its Platform handle; the run log showed the NewObject failing on a missing
 * <init>. calloc leaves the vptr zero, hence the placement-new. */
static void platform_ctor(JNIEnv *, jobject self, jclass) { new (self) RR3Platform(); }

static const ManagedMethod platform_methods[] = {
    ManagedMethod::RegisterNonVirtual<&platform_ctor>(RR3Platform::clazz, "<init>", "()V"),
    ManagedMethod::RegisterStatic<&platform_show_message>(
        RR3Platform::clazz, "showMessage",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;III)V"),
    ManagedMethod::RegisterStatic<&platform_is_network_available>(
        RR3Platform::clazz, "isNetworkAvailable", "(Z)Z"),
    ManagedMethod::RegisterStatic<&platform_is_mobile_data_available>(
        RR3Platform::clazz, "isMobileDataAvailable", "()Z"),
    ManagedMethod::RegisterStatic<&platform_is_network_settings_shown>(
        RR3Platform::clazz, "isNetworkSettingsShown", "()Z"),
    ManagedMethod::RegisterStatic<&platform_open_network_settings>(
        RR3Platform::clazz, "openNetworkSettings", "()V"),
    ManagedMethod::RegisterStatic<&platform_get_locale>(
        RR3Platform::clazz, "getLocale", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_carrier>(
        RR3Platform::clazz, "getCarrier", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_set_crashlytics_int>(
        RR3Platform::clazz, "setCrashlyticsInt", "(ILjava/lang/String;)V"),
    ManagedMethod::RegisterStatic<&platform_set_crashlytics_string>(
        RR3Platform::clazz, "setCrashlyticsString",
        "(Ljava/lang/String;Ljava/lang/String;)V"),
    ManagedMethod::RegisterStatic<&platform_delete_directory>(
        RR3Platform::clazz, "deleteDirectory", "(Ljava/lang/String;)Z"),
    ManagedMethod::RegisterStatic<&platform_get_total_memory>(
        RR3Platform::clazz, "getTotalMemory", "()I"),
    ManagedMethod::RegisterStatic<&platform_exit_app>(
        RR3Platform::clazz, "exitApp", "()V"),
    ManagedMethod::RegisterStatic<&platform_get_device_uid>(
        RR3Platform::clazz, "getDeviceUID", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_model_id>(
        RR3Platform::clazz, "getModelID", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_model_name>(
        RR3Platform::clazz, "getModelName", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_app_name>(
        RR3Platform::clazz, "getAppName", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_app_version>(
        RR3Platform::clazz, "getAppVersion", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_os_version>(
        RR3Platform::clazz, "getOsVersion", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&platform_get_api_level>(
        RR3Platform::clazz, "getApiLevel", "()I"),
    ManagedMethod::RegisterStatic<&platform_get_app_install_time>(
        RR3Platform::clazz, "getAppInstallTime", "()J"),
    ManagedMethod::RegisterStatic<&platform_get_app_memory_free>(
        RR3Platform::clazz, "getAppMemoryFree", "()J"),
    ManagedMethod::RegisterStatic<&platform_get_app_memory_usage>(
        RR3Platform::clazz, "getAppMemoryUsage", "()J"),
    ManagedMethod::RegisterStatic<&platform_memory_probe>(
        RR3Platform::clazz, "memoryProbe", "()V"),
    ManagedMethod::RegisterStatic<&platform_get_screen_width>(
        RR3Platform::clazz, "getScreenWidth", "()I"),
    ManagedMethod::RegisterStatic<&platform_get_screen_height>(
        RR3Platform::clazz, "getScreenHeight", "()I"),
    ManagedMethod::RegisterStatic<&platform_get_screen_dpi>(
        RR3Platform::clazz, "getScreenDPI", "()I"),
    ManagedMethod::RegisterStatic<&platform_get_screen_rotation>(
        RR3Platform::clazz, "getScreenRotation", "()I"),
    ManagedMethod::RegisterStatic<&platform_open_url>(
        RR3Platform::clazz, "openURL", "(Ljava/lang/String;)V"),
    ManagedMethod::RegisterStatic<&platform_save_image>(
        RR3Platform::clazz, "saveImage", "([IIIILjava/lang/String;)Z"),
    ManagedMethod::RegisterStatic<&platform_save_image>(
        RR3Platform::clazz, "saveToImageGallery", "([IIIILjava/lang/String;)Z"),
    ManagedMethod::RegisterStatic<&platform_load_texture_from_memory>(
        RR3Platform::clazz, "loadTextureFromMemory",
        "([BI)Lcom/firemint/realracing3/TextureInfo;"),
    ManagedMethod::RegisterStatic<&platform_toggle_idle_mode>(
        RR3Platform::clazz, "toggleIdleMode", "(Z)V"),
    ManagedMethod::RegisterStatic<&platform_test_func>(
        RR3Platform::clazz, "testFunc", "(I)I"),
    {NULL},
};

Class RR3Platform::clazz = {
    .classpath = "com/firemint/realracing3/Platform",
    .classname = "Platform",
    .managed_methods = platform_methods,
    .native_methods = {NULL},
    .fields = {NULL},
    /* The Java class is all statics, but the engine still builds one instance
     * as its handle - measured, the first run with this class registered logged
     * "Class Platform does not have method <init>()V." */
    .instance_size = sizeof(RR3Platform),
};

static const int platform_registered = ClassRegistry::register_class(RR3Platform::clazz);
