#ifndef REALRACING3_RR3_SAVEFILE_PATCH_H
#define REALRACING3_RR3_SAVEFILE_PATCH_H

#include "so_util.h"

/*
 * Stop SaveManager's temp-file scan from throwing std::out_of_range on any
 * filename that has no dot in it. See the file comment in
 * rr3_savefile_patch.cpp for the backtrace and the one-word patch.
 *
 * Same placement rule as rr3_apply_asset_patches(): after so_relocate_all()
 * and before so_initialize(), i.e. from so_after_relocate().
 */
void rr3_apply_savefile_patch(so_module *mod);

#endif /* REALRACING3_RR3_SAVEFILE_PATCH_H */
