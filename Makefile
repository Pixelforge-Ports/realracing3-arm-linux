# Real Racing 3 loader.
#
# Always cross-compiles to arm-linux-gnueabihf: the R36S runs an aarch64
# kernel, but the only native library the game ships is armeabi-v7a, and the
# bionic ELF loader has to map it into a process of the same word size.
#
# Sources are picked up by wildcard on purpose — the loader, the JNI shim and
# the libc thunks all land as new directories in later milestones and must not
# each require a Makefile edit.

ARCH       ?= arm-linux-gnueabihf
CROSS      ?= $(ARCH)-
CXX        := $(CROSS)g++
PKG_CONFIG ?= $(CROSS)pkg-config

TARGET  := build/realracing3
OBJDIR  := build/obj

PKGS := sdl2 zlib libzip

OPT  ?= -O2 -g
WARN := -Wall -Wextra -Wno-unused-parameter -Werror=return-type

# thunks/libc/generated holds the bionic export tables produced by
# tools/generate_libc.sh. They are checked in because the build image has no
# clang; see that script for why.
INCLUDES := -I. -Isrc -Iandroid -Iloader -Ithunks -Ithunks/libc \
            -Ithunks/libc/generated -Ijni
CPPFLAGS := $(INCLUDES) $(shell $(PKG_CONFIG) --cflags $(PKGS)) -MMD -MP
# gnu++20: the vendored ELF loader uses std::string::starts_with.
CXXFLAGS := -std=gnu++20 $(OPT) $(WARN) -fno-strict-aliasing -fuse-cxa-atexit
LDFLAGS  := $(OPT)
LDLIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS)) -pthread -lm -ldl -lrt -lbsd

# Keep the active bootstrap intentionally small. The directory began as the
# loader scaffold from an earlier port; files that have not yet been validated
# for RR3 remain available as reference but are not linked merely because they
# exist.
SRCS := \
  src/atc_decompress.cpp src/crash.cpp src/dxt_decompress.cpp src/gl_diag.cpp \
  src/gl_probe.cpp src/gl_stats.cpp src/sdl_info.cpp \
  src/main.cpp src/symtab.cpp src/symtab_bionic.cpp src/symtab_gl_stubs.cpp \
  src/symtab_glprobe.cpp src/symtab_io.cpp src/symtab_libm.cpp \
  src/symtab_net.cpp src/symtab_off.cpp src/symtab_pthread.cpp \
  src/symtab_sem.cpp src/symtab_setjmp.cpp src/symtab_stat.cpp \
  src/symtab_time.cpp src/symtab_unwind.cpp src/trace.cpp src/unwind_arm.cpp \
  src/symtab_zlib.cpp src/rr3_control.cpp src/rr3_asset_patch.cpp src/rr3_savefile_patch.cpp \
  src/rr3_control_scheme.cpp src/rr3_texture_guard.cpp src/rr3_tutorial_trace.cpp \
  src/rr3_fmod_pump.cpp \
  android/app_exit.cpp android/asset_manager.cpp android/egl_shim.cpp android/fb_probe.cpp \
  android/log.cpp android/opensles.cpp android/platform.cpp android/input_bridge.cpp \
  $(wildcard loader/*.cpp) $(wildcard thunks/libc/*.cpp) \
  $(wildcard thunks/khronos/*.cpp) jni/class_registry.cpp jni/jni.cpp \
  jni/classes/bytebuffer.cpp jni/classes/lang_ClassLoader.cpp \
	jni/classes/rr3_MainActivity.cpp \
	jni/classes/rr3_Font.cpp \
	jni/classes/rr3_font_face.cpp \
	jni/classes/rr3_GlyphVector.cpp \
	jni/classes/rr3_AudioStreamManager.cpp jni/classes/rr3_LocalNotifications.cpp \
	jni/classes/cloudcell_compat.cpp \
	jni/classes/cloudcell_defer.cpp \
	jni/classes/cc_http_request.cpp \
	jni/classes/cc_services.cpp \
	jni/classes/rr3_Platform.cpp \
  jni/classes/string.cpp third_party/powervr/PVRTDecompress.cpp \
  third_party/stb/stb_truetype.cpp
OBJS    := $(patsubst %.cpp,$(OBJDIR)/%.cpp.o,$(SRCS))
DEPS    := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# The .so files the zip has to carry. Generated, never checked in: they are
# Debian binaries and tools/collect_libs.sh reproduces them exactly.
libs: $(TARGET)
	tools/collect_libs.sh $(TARGET) build/libs.armhf
	tools/check_glibc_floor.sh $(TARGET) build/libs.armhf

clean:
	rm -rf build

.PHONY: all clean libs

-include $(DEPS)
