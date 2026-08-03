/*
 * Why is the driving tutorial not advancing? Read the answer instead of
 * guessing it.
 *
 * The tutorial's whole phase-0 body sits behind one gate. From
 * TutorialMode::OnUpdateGame (0x776d7c), with fp holding `this`:
 *
 *     776ed0  add  r8, fp, #324      ; r8 = &this->m_taskQueue  (this+0x144)
 *     776edc  bl   GameTaskQueue::Update(dt)
 *     776ee0  ldr  r5, [fp, #140]    ; phase  (this+0x8c): 0, 1 or 2
 *     776fc4  mov  r0, r8
 *     776fc8  bl   GameTaskQueue::AreAllTasksComplete()
 *     776fcc  cmp  r0, #0
 *     776fd0  beq  776efc            ; -> function epilogue, nothing else runs
 *     776fe4  ldr  r3, [fp, #92]     ; timer  (this+0x5c)
 *     776ff0  add  r3, r3, r7        ; timer += dt
 *     776ff4  str  r3, [fp, #92]
 *     ...     Car::SetCanSteer(true), Car::PlayerSteering(dt), then the
 *             switch on this+0x68 that is the tutorial's state machine
 *
 * So a single pending task freezes the timer, the state machine and the
 * player's steering all at once, and it looks exactly like "the game ignores
 * my input". The distinction matters because the two have opposite fixes.
 *
 * AreAllTasksComplete (0x6dbf28) needs no call at all - it is four
 * instructions, "return queue[0x18] == queue[0x08]" - so this hook only reads
 * memory. Its sibling AreDelayedTasksComplete (0x6dbf40) compares 0x28/0x2c;
 * both pairs are printed because GameTaskQueue::Update runs the delayed list
 * separately and only the first pair is the gate.
 *
 * The hook itself is reentrant: unhook, call the original, hook again. There
 * is one game thread calling OnUpdateGame, so no locking is needed here.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rr3_tutorial_trace.h"
#include "trace.h"

/* Field offsets read off the disassembly quoted above. */
static const uintptr_t kTimerOffset = 0x5c;
static const uintptr_t kStateOffset = 0x68;
static const uintptr_t kPhaseOffset = 0x8c;
static const uintptr_t kQueueOffset = 0x144;

/* GameTaskQueue's two iterator pairs, from AreAllTasksComplete /
 * AreDelayedTasksComplete. */
static const uintptr_t kTasksCurrent = 0x08;
static const uintptr_t kTasksEnd     = 0x18;
static const uintptr_t kDelayedBegin = 0x28;
static const uintptr_t kDelayedEnd   = 0x2c;

static ReentrantHook g_hook;
static bool g_installed = false;
static uintptr_t g_module_base = 0;

static uint32_t read_u32(const void *self, uintptr_t offset)
{
    uint32_t value = 0;
    memcpy(&value, (const unsigned char *)self + offset, sizeof(value));
    return value;
}

static void tutorial_on_update_game(void *self, int dt)
{
    const unsigned char *queue = (const unsigned char *)self + kQueueOffset;
    uint32_t tasks_current = read_u32(queue, kTasksCurrent);
    uint32_t tasks_end = read_u32(queue, kTasksEnd);
    uint32_t delayed_begin = read_u32(queue, kDelayedBegin);
    uint32_t delayed_end = read_u32(queue, kDelayedEnd);
    uint32_t phase = read_u32(self, kPhaseOffset);
    uint32_t state = read_u32(self, kStateOffset);
    uint32_t timer = read_u32(self, kTimerOffset);
    bool complete = tasks_current == tasks_end;

    /* Which task is holding the queue open. The deque stores GameTask*, so the
     * front element's vtable names its class: subtract the module base and
     * look the offset up against _ZTV<Task> in the donor .so. Both tasks the
     * tutorial queues are added by TutorialMode::OnTrackLoaded (0x7761b8 and
     * 0x7761e0) and a third, CountdownGo, at 0x7778e8. */
    uint32_t front_vtable = 0;
    if (!complete && tasks_current) {
        uint32_t front = *(const uint32_t *)(uintptr_t)tasks_current;
        if (front)
            front_vtable = *(const uint32_t *)(uintptr_t)front;
    }

    /* One line per change plus a heartbeat: a frame under qemu is seconds
     * long, and a per-frame line would bury everything else in the log. */
    static uint32_t last_phase = 0xffffffff;
    static uint32_t last_state = 0xffffffff;
    static bool last_complete = false;
    static unsigned long calls = 0;
    bool changed = phase != last_phase || state != last_state ||
                   complete != last_complete;
    if (changed || (calls % 20) == 0)
        trace("tutorial: phase=%u state=%u timer=%u dt=%d tasksComplete=%d "
              "(cur=%08x end=%08x front_vtable=+0x%lx) delayed=%d%s",
              phase, state, timer, dt, complete ? 1 : 0,
              tasks_current, tasks_end,
              front_vtable ? (unsigned long)(front_vtable - g_module_base) : 0UL,
              delayed_begin == delayed_end ? 1 : 0,
              changed ? " <-" : "");
    last_phase = phase;
    last_state = state;
    last_complete = complete;
    calls++;

    rehook_unhook(&g_hook);
    ((void (*)(void *, int))g_hook.addr)(self, dt);
    rehook_hook(&g_hook);
}

void rr3_install_tutorial_trace(so_module *mod)
{
    const char *env = getenv("REALRACING3_TUTORIAL_TRACE");
    if (!env || !*env || strcmp(env, "0") == 0)
        return;

    uintptr_t addr = so_symbol(mod, "_ZN12TutorialMode12OnUpdateGameEi");
    if (!addr) {
        trace("tutorial: TutorialMode::OnUpdateGame not exported, no trace");
        return;
    }
    if (g_installed)
        return;

    /* rehook_new() ends with rehook_unhook(): it records the two code shapes
     * and leaves the ORIGINAL prologue in place, so the hook has to be armed
     * explicitly. Without this the install line prints and nothing else ever
     * does - measured, and it cost a full ten-minute run. */
    rehook_new(mod, &g_hook, addr, (uintptr_t)&tutorial_on_update_game);
    rehook_hook(&g_hook);
    g_installed = true;
    g_module_base = mod->base;
    trace("tutorial: tracing TutorialMode::OnUpdateGame at %p (module base %p)",
          (void *)addr, (void *)mod->base);
}
