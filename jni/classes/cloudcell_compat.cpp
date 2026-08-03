#include "jni.h"
#include "jni_internals.h"

/* RR3 2.7 invokes these Cloudcell Java facades while checking for optional
 * online services. The native game already owns the extracted donor data, so
 * the Android service only needs to exist and report stable device metadata;
 * downloads and ads are intentionally no-ops for the offline port. */
static Class asset_service = {
    .classpath = "com/firemonkeys/cloudcellapi/AndroidAssetManagerService",
    .classname = "AndroidAssetManagerService",
    .managed_methods = {NULL}, .native_methods = {NULL}, .fields = {NULL},
    .instance_size = sizeof(Object),
};
static void asset_ctor(JNIEnv *, jobject, jclass) {}
static const ManagedMethod asset_methods[] = {
    ManagedMethod::RegisterNonVirtual<&asset_ctor>(asset_service, "<init>", "()V"),
    {NULL},
};
struct AssetHook { AssetHook() { asset_service.managed_methods = asset_methods; } } asset_hook;
static const int asset_registered = ClassRegistry::register_class(asset_service);

static Class cc_activity = {
    .classpath = "com/firemonkeys/cloudcellapi/CC_Activity",
    .classname = "CC_Activity",
    .managed_methods = {NULL}, .native_methods = {NULL}, .fields = {NULL},
    .instance_size = sizeof(Object),
};
static void start_download_service(JNIEnv *, jclass) {}
/* Report offline so the game takes its local-cache path instead of waiting for
 * a dead Cloudcell license/update endpoint. */
static jint get_network_connectivity(JNIEnv *, jclass) { return 0; }
static const ManagedMethod activity_methods[] = {
    ManagedMethod::RegisterStatic<&start_download_service>(
        cc_activity, "startDownloadService", "()V"),
    ManagedMethod::RegisterStatic<&get_network_connectivity>(
        cc_activity, "getNetworkConnectivity", "()I"),
    {NULL},
};
struct ActivityHook { ActivityHook() { cc_activity.managed_methods = activity_methods; } } activity_hook;
static const int activity_registered = ClassRegistry::register_class(cc_activity);

static Class get_info = {
    .classpath = "com/firemonkeys/cloudcellapi/util/GetInfo",
    .classname = "GetInfo",
    .managed_methods = {NULL}, .native_methods = {NULL}, .fields = {NULL},
    .instance_size = sizeof(Object),
};
static jstring info_string(JNIEnv *env, jclass)
{
    return env->NewStringUTF("R36S");
}
static jstring package_name(JNIEnv *env, jclass) { return env->NewStringUTF("com.ea.games.r3_row"); }
static jstring build_version(JNIEnv *env, jclass) { return env->NewStringUTF("2.7.0"); }
static jstring device_uid(JNIEnv *env, jclass) { return env->NewStringUTF("rr3-r36s-local"); }
static jboolean get_advertising_enabled(JNIEnv *, jclass) { return JNI_FALSE; }
static const ManagedMethod info_methods[] = {
    ManagedMethod::RegisterStatic<&info_string>(get_info, "GetDeviceModel", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&info_string>(get_info, "GetDeviceFirmwareVersion", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&info_string>(get_info, "GetDeviceManufacturer", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&build_version>(get_info, "GetBuildVersion", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&package_name>(get_info, "GetPackageName", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&info_string>(get_info, "GetDeviceMacAddress", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&device_uid>(get_info, "GetDeviceUID", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&info_string>(get_info, "GetAdvertisingID", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&info_string>(get_info, "GetLocalIp", "()Ljava/lang/String;"),
    ManagedMethod::RegisterStatic<&get_advertising_enabled>(get_info, "GetAdvertisingEnabled", "()Z"),
    {NULL},
};
struct InfoHook { InfoHook() { get_info.managed_methods = info_methods; } } info_hook;
static const int info_registered = ClassRegistry::register_class(get_info);
