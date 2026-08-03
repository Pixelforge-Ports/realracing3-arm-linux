#ifndef RR3_TEXTURE_GUARD_H
#define RR3_TEXTURE_GUARD_H

#include "so_util.h"

/* Installs a null-data guard on mtTextureGL's decoders. Call once the module is
 * mapped and relocated, before the engine loads its first texture. */
void rr3_texture_guard_init(so_module *mod);

/* Prints how many decodes were refused. Call at shutdown: on a healthy donor it
 * prints nothing at all. */
void rr3_texture_guard_report(void);

#endif
