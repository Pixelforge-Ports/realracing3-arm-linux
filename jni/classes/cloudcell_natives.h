#pragma once

#include "jni.h"
#include "so_util.h"

/*
 * Resolving the game's own Java_* exports.
 *
 * Class::native_methods / jni_resolve_native() cannot be used for these:
 * they match by DT_SONAME and libRealRacing3.so has none, so
 * so_load_module() skips the whole resolution pass (loader/so_util.cpp:489).
 * Every Cloudcell class therefore leaves .native_methods = {NULL} and looks its
 * callbacks up by name, the same way android/input_bridge.cpp:1375 does for the
 * input entry points.
 *
 * Lazily, because the facades are constructed long after so_load_module() but
 * the ManagedMethod tables are built during static initialisation, before the
 * module exists.
 *
 * No pcs("aapcs") on any of these prototypes: unlike the input callbacks, not
 * one Cloudcell callback takes a float or a double, and for integer-only
 * argument lists the hard-float and soft-float PCS are identical.
 */
/* Defined in src/main.cpp with C++ linkage, like every other consumer of it. */
so_module *realracing3_module(void);

template <typename Fn>
static inline Fn cloudcell_native(const char *name)
{
    so_module *mod = realracing3_module();
    if (!mod)
        return (Fn)NULL;
    return (Fn)so_symbol(mod, name);
}
