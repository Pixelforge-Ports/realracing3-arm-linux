/*
 * com.firemint.realracing3.AudioStreamManager
 *
 * The first hardware run reached gameplay with no sound at all, and the log
 * named the reason:
 *
 *     FindClass: no fake class registered for
 *     'com/firemint/realracing3/AudioStreamManager' - every method the game
 *     looks up on it will come back NULL.
 *
 * On Android this class does nothing the game's audio depends on: it owns an
 * FMODAudioDevice, asks the platform AudioManager for the media stream, and
 * follows the Activity lifecycle so playback ducks when another app takes
 * focus (decompiled source: constructor, available(), setMediaStreamSolo(),
 * onStart/onStop/onPause/onResume, setAudioManager, and the two static
 * lifecycle entry points). None of that exists on a handheld running one game
 * at a time.
 *
 * What it does need is to EXIST. The engine calls FindClass on it during audio
 * init, and a class that is not registered makes every later method lookup
 * return NULL - which is how a missing focus helper turned into silence.
 *
 * So every method is a no-op that returns cleanly. available() answers true:
 * the audio device is there, it just is not Android's.
 */
#include "jni.h"
#include "jni_internals.h"

static Class clazz = {
    .classpath = "com/firemint/realracing3/AudioStreamManager",
    .classname = "AudioStreamManager",
    .managed_methods = {NULL},
    .native_methods = {NULL},
    .fields = {NULL},
    .instance_size = sizeof(Object),
};

static void ctor(JNIEnv *, jobject, jclass) {}
/* The third parameter is required by the dispatch template: instance
 * methods are registered with (JNIEnv*, jobject, jclass) even when the
 * class argument is unused. */
static void lifecycle(JNIEnv *, jobject, jclass) {}
static void static_lifecycle(JNIEnv *, jclass) {}
static void set_audio_manager(JNIEnv *, jobject, jclass, jobject) {}
static jboolean available(JNIEnv *, jobject, jclass) { return JNI_TRUE; }

static const ManagedMethod methods[] = {
    ManagedMethod::RegisterNonVirtual<&ctor>(clazz, "<init>", "()V"),
    ManagedMethod::RegisterNonVirtual<&lifecycle>(clazz, "onStart", "()V"),
    ManagedMethod::RegisterNonVirtual<&lifecycle>(clazz, "onStop", "()V"),
    ManagedMethod::RegisterNonVirtual<&lifecycle>(clazz, "onPause", "()V"),
    ManagedMethod::RegisterNonVirtual<&lifecycle>(clazz, "onResume", "()V"),
    ManagedMethod::RegisterNonVirtual<&available>(clazz, "available", "()Z"),
    ManagedMethod::RegisterNonVirtual<&set_audio_manager>(
        clazz, "setAudioManager", "(Landroid/media/AudioManager;)V"),
    ManagedMethod::RegisterStatic<&static_lifecycle>(clazz, "staticOnPause", "()V"),
    ManagedMethod::RegisterStatic<&static_lifecycle>(clazz, "staticOnResume", "()V"),
    {NULL},
};

/* Anonymous namespace: the sibling class files use this same idiom with the
 * same name, and at file scope they collide at link time. */
namespace { struct Hook { Hook() { clazz.managed_methods = methods; } } hook; }
static const int registered = ClassRegistry::register_class(clazz);
