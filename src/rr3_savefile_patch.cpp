/*
 * SaveManager's temp-file scan throws on any filename without a dot.
 *
 * The backtrace, taken from the guest abort() with the stack scanner in
 * src/crash.cpp (innermost first, module offsets):
 *
 *     0x00920fb4  <static helper>            <- __cxa_throw of out_of_range
 *     0x009226f4  SaveManager::CreateTempFileWithSaveGame()+0xd4
 *     0x00922a44  SaveManager::GetSaveDataFile(bool, void*)+0x24
 *     0x0093fcc4  CC_SyncManager_Class::QueueBlob(...)+0x62
 *     0x0093f836  CC_SyncManager_Class::QueueSync()+0x4e
 *     0x00922d60  SaveManager::LoadGameData()+0x2ec
 *     0x00437a2c  CGlobal::scene_LoadCharacter()+0x20
 *     0x0043deb8  CGlobal::scene_DoIncrementalLoad()+0x80
 *     0x007bfb38  GuiComponent::Update(int)+0x54
 *     0x0044ccb4  Splash::Update(int)+0xe4
 *     0x00440070  CGlobal::scene_Update(int)+0x548
 *
 * with libstdc++ printing only
 *
 *     terminate called after throwing an instance of 'std::out_of_range'
 *       what():  basic_string::substr
 *
 * CreateTempFileWithSaveGame (0x922620) lists the document directory -
 * FileSystem::GetDirListingAbsolute(GetDocPath(), NULL, &files, true) at
 * 0x9226ac - and runs every filename through a 24-instruction static helper at
 * 0x920f18, whose whole job is "does this name end in the temp-save
 * extension?":
 *
 *     920f34  pos = name.rfind(".", npos, 1)
 *     920f48  cmp  pos, name.size()
 *     920f4c  bhi  920fa8              -> __throw_out_of_range("basic_string::substr")
 *     920f64  sub  = name.substr(pos, npos)
 *     920f78  cmp  = sub.compare(SaveManager::scm_tempFileName)
 *
 * A name with no dot makes rfind return npos, npos is greater than size(), and
 * substr throws. Nothing catches it, so std::terminate -> abort.
 *
 * Why it only shows up now: before the readdir fix in src/symtab_bionic.cpp the
 * listing came back empty on every call, so the loop body never ran. With the
 * listing populated, the document directory - which in this port is the donor
 * tree itself - contains Cloudcell blob-store files named "1", "3", "7", "12"
 * and the first of them throws.
 *
 * The patch is one word: turn the throwing branch into a clamp.
 *
 *     8a000015   bhi  920fa8        ->   81a00003   movhi r0, r3
 *
 * pos becomes size(), substr(size(), npos) is the empty string, the compare
 * against scm_tempFileName fails, and the helper answers "no, not a temp file"
 * - which is the truthful answer for a name that has no extension at all, and
 * exactly what the caller (a counter of temp saves) wants.
 *
 * Not a blind offset: the helper is located from the exported symbol that
 * precedes it, and the instruction is only rewritten if it still reads as the
 * expected BHI, so a different donor build fails the check and is left alone.
 */

#include <stdint.h>
#include <string.h>

#include "rr3_savefile_patch.h"
#include "trace.h"

/*
 * The helper is static, so it has no symbol of its own. It is the tail of the
 * same 0x100-byte block that starts at SaveManager::BackupCharacterUIDCallback
 * (0x920f00, size 24) - the compiler emitted it right behind that callback, at
 * +0x18. Anchoring on the exported neighbour keeps the load-time address
 * correct whatever base the module is mapped at.
 */
static const char kAnchorSymbol[] = "_ZN11SaveManager26BackupCharacterUIDCallbackEPv";
static const uintptr_t kHelperFromAnchor = 0x18;
static const uintptr_t kBranchInHelper   = 0x34;   /* 0x920f4c - 0x920f18 */

static const uint32_t kExpectedBranch = 0x8a000015; /* bhi  +0x54 (the throw) */
static const uint32_t kClampInstead   = 0xe1a00003 | (0x8u << 28); /* movhi r0, r3 */

void rr3_apply_savefile_patch(so_module *mod)
{
    uintptr_t anchor = so_symbol(mod, kAnchorSymbol);

    /* SaveManager is ARM (it sits far below the Thumb-only Cloudcell range
     * 0x9298b0-0x962400), so a set low bit means this is not the build these
     * offsets were read from. */
    if (!anchor || (anchor & 1)) {
        trace("savefile patch: %s not found or not ARM - substr throw left in "
              "place", kAnchorSymbol);
        return;
    }

    uint32_t *branch = (uint32_t *)(anchor + kHelperFromAnchor + kBranchInHelper);

    if (*branch != kExpectedBranch) {
        trace("savefile patch: instruction at anchor+0x%lx reads 0x%08x, "
              "expected 0x%08x - not patching",
              (unsigned long)(kHelperFromAnchor + kBranchInHelper),
              *branch, kExpectedBranch);
        return;
    }

    *branch = kClampInstead;
    trace("savefile patch: dotless filenames no longer throw out_of_range in "
          "SaveManager's temp-file scan");
}
