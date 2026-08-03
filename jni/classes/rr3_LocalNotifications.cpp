#include "jni.h"
#include "jni_internals.h"

static Class clazz = {
    .classpath = "com/firemint/realracing3/LocalNotificationsCenter",
    .classname = "LocalNotificationsCenter",
    .managed_methods = {NULL},
    .native_methods = {NULL},
    .fields = {NULL},
    .instance_size = sizeof(Object),
};
static void cancel_all(JNIEnv *, jclass) {}
static const ManagedMethod methods[] = {
    ManagedMethod::RegisterStatic<&cancel_all>(clazz, "CancelAllNotifications", "()V"),
    {NULL},
};
struct Hook { Hook() { clazz.managed_methods = methods; } } hook;
static const int registered = ClassRegistry::register_class(clazz);
