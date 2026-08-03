#ifndef REALRACING3_RR3_TUTORIAL_TRACE_H
#define REALRACING3_RR3_TUTORIAL_TRACE_H

#include "so_util.h"

/*
 * Diagnostics only, behind REALRACING3_TUTORIAL_TRACE=1. Reports why the
 * driving tutorial is or is not advancing; see the file comment in
 * rr3_tutorial_trace.cpp.
 *
 * Same placement rule as the other patches: from so_after_relocate(), while
 * the text is still writable and none of the game's code has run.
 */
void rr3_install_tutorial_trace(so_module *mod);

#endif /* REALRACING3_RR3_TUTORIAL_TRACE_H */
