/*
 * Null-data guard for mtTextureGL's three decoders.
 *
 * First hardware run to survive both loading screens died here:
 *
 *     [I/libRealRacing3] deScrambleDataDecompression error: Z_DATA_ERROR
 *     FATAL: SIGSEGV at 0x00000000
 *            pc = libRealRacing3.so+0x00844f98   mtTextureGL::LoadFromDataDds
 *            lr = libRealRacing3.so+0x008465e4   mtTextureGL::load
 *            r1 = 0x00000000
 *
 * 679 MB were still free, so this is not memory pressure. The engine's own
 * descrambler failed on a texture, returned nothing, and mtTextureGL::load
 * passed that nothing straight into the DDS decoder, which dereferences the
 * pointer four instructions in. On Android the asset was always there, so the
 * missing check never mattered; here it is the difference between a missing
 * texture and a dead process.
 *
 * The guard refuses the call and reports failure - the same answer the decoder
 * gives for a corrupt file it managed to read - so the engine takes a path it
 * already knows how to take. All three decoders get it, not only the one that
 * crashed: they are called from the same place with the same buffer, and a
 * donor missing a PVR would fail identically.
 *
 * Returning 0 is safe whatever the real return type is. If these are void, the
 * caller ignores r0; if they report success, 0 is the failure answer.
 *
 * This is deliberately a survival measure, not a fix. Why the descrambling
 * fails at all is still open (the asset patch reports 20000 of 34861 entries
 * absent), and the count printed on exit is the measure of how much art is
 * silently missing.
 */
#include <atomic>
#include <stdint.h>
#include <stdio.h>

#include "so_util.h"
#include "trace.h"

#include "rr3_texture_guard.h"

namespace {

struct Decoder {
    const char   *symbol;
    const char   *name;
    ReentrantHook hook;
    std::atomic<long> refused;
};

/* Signatures from the donor's symbol table:
 *   _ZN11mtTextureGL15LoadFromDataDdsEPKhji  (const uchar*, uint, int)
 *   _ZN11mtTextureGL15LoadFromDataPvrEPKhji  (const uchar*, uint, int)
 *   _ZN11mtTextureGL15LoadFromDataTgaEPKhj   (const uchar*, uint)
 * Tga takes one argument less; calling it through the wider prototype is
 * harmless on this ABI because the extra register is simply never read. */
Decoder g_decoders[] = {
    { "_ZN11mtTextureGL15LoadFromDataDdsEPKhji", "Dds", {}, {0} },
    { "_ZN11mtTextureGL15LoadFromDataPvrEPKhji", "Pvr", {}, {0} },
    { "_ZN11mtTextureGL15LoadFromDataTgaEPKhj",  "Tga", {}, {0} },
};

using DecodeFn = int (*)(void *, const unsigned char *, unsigned int, int);

int call_original(Decoder &d, void *self, const unsigned char *data,
                  unsigned int size, int arg)
{
    /* Unhook, call, rehook: the trampoline overwrites the prologue, so the
     * original has to be restored for the duration of the real call. */
    rehook_unhook(&d.hook);
    int result = ((DecodeFn)d.hook.addr)(self, data, size, arg);
    rehook_hook(&d.hook);
    return result;
}

int guard(Decoder &d, void *self, const unsigned char *data,
          unsigned int size, int arg)
{
    if (data && size)
        return call_original(d, self, data, size, arg);

    long n = ++d.refused;
    /* Loud once, then sampled: a donor missing a whole directory would
     * otherwise print thousands of identical lines. Braces are required -
     * warning() and trace() are multi-statement macros. */
    if (n == 1) {
        warning("texture guard: refused a %s decode with no data - the engine's "
                "descrambler returned nothing. This would have been a SIGSEGV; "
                "the texture is missing instead.\n", d.name);
    } else if (n % 100 == 0) {
        trace("texture guard: %ld %s decodes refused so far", n, d.name);
    }
    return 0;
}

int guard_dds(void *s, const unsigned char *d, unsigned int n, int a)
{
    return guard(g_decoders[0], s, d, n, a);
}
int guard_pvr(void *s, const unsigned char *d, unsigned int n, int a)
{
    return guard(g_decoders[1], s, d, n, a);
}
int guard_tga(void *s, const unsigned char *d, unsigned int n, int a)
{
    return guard(g_decoders[2], s, d, n, a);
}

const DecodeFn kGuards[] = { guard_dds, guard_pvr, guard_tga };

} // namespace

void rr3_texture_guard_init(so_module *mod)
{
    for (size_t i = 0; i < sizeof(g_decoders) / sizeof(g_decoders[0]); i++) {
        Decoder &d = g_decoders[i];
        uintptr_t addr = so_symbol(mod, d.symbol);
        if (!addr) {
            /* Not fatal: a different build may not export every decoder. Say
             * so, because a silently absent guard looks exactly like a guard
             * that never had to fire. */
            warning("texture guard: %s decoder not found in this build; "
                    "it stays unguarded\n", d.name);
            continue;
        }
        rehook_new(mod, &d.hook, addr, (uintptr_t)kGuards[i]);
    }
    trace("texture guard: installed on the mtTextureGL decoders");
}

void rr3_texture_guard_report(void)
{
    long total = 0;
    for (size_t i = 0; i < sizeof(g_decoders) / sizeof(g_decoders[0]); i++)
        total += g_decoders[i].refused.load();
    if (total)
        trace("texture guard: %ld texture(s) never decoded (Dds=%ld Pvr=%ld "
              "Tga=%ld) - that many blanks on screen, but no crash",
              total, g_decoders[0].refused.load(),
              g_decoders[1].refused.load(), g_decoders[2].refused.load());
}
