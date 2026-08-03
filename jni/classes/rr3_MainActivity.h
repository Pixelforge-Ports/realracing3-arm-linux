#pragma once

#include "jni.h"
#include "jni_internals.h"

/* Minimal Java-side receiver for com.firemint.realracing3.MainActivity. */
class RR3MainActivity : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }
};
