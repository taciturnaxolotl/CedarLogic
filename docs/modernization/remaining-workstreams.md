# Modernization plan: remaining workstreams

This document specs the three modernization workstreams that are **not** safe to
land as blind, unattended changes, so that the approach can be reviewed and
greenlit before implementation. The earlier workstreams shipped as their own
PRs: self-contained `.cdl` files, typed inter-thread messages, the headless
`--render` harness, expanded engine tests, and the collision spatial grid.

The through-line for those was a testable seam: a self-contained algorithm that
could be made wx-free and pinned by a differential test (the collision grid is
the model — a broad phase proven equivalent to brute force across thousands of
random scenes). The three workstreams below lack that seam today. Each section
says what stands in the way, the concrete steps to change it, and how we'd know
it still works.

---

## C — Dissolve the `wxGetApp()` singleton

**Current state.** `wxGetApp()` is called **175 times across 23 files**. It is
the de-facto service locator for the gate libraries
(`libraries`, `gateNameToLibrary`, `libParser`), the inter-thread message queues
(`dGUItoLOGIC`, `dLOGICtoGUI`), the synchronization primitives, and app settings.
The heaviest users are `MainFrame.cpp` (70), `GUICanvas.cpp` (17),
`threadLogic.cpp` (12), and `GUICircuit.cpp` (11).

Two safe slices have already landed or are trivial:
- Removing *vestigial* coupling — files that pulled in `MainApp.h` /
  `DECLARE_APP` but never called `wxGetApp()` (already done for the collision
  checker and four GUI helpers).

**Plan (incremental, one seam at a time).**
1. Group the singleton's members by concern: **gate library** (3 maps +
   `LibraryParse`), **message bus** (2 deques + their mutex), **sync**
   (`m_critsect`, `m_semAllDone`, `simulate`, `readyToSend`), **settings**.
2. Introduce a plain struct per concern (e.g. `GateLibrary`, `MessageBus`) owned
   by `MainApp` but passed by reference into the subsystems that use it.
3. Convert one concern at a time, starting with **gate library** — it is
   read-mostly and already near-standalone (`LibraryParse` is its own object).
   Thread a `GateLibrary&` into `CircuitParse`, `PaletteFrame`, and the load
   path instead of reaching for `wxGetApp().libraries`.
4. Leave `wxGetApp()` as a thin shim delegating to the new objects until every
   call site is migrated, so each step compiles and runs unchanged.

**Risk.** Wide surface; easy to introduce lifetime/ordering bugs. Low
*algorithmic* risk but high *blast radius*.

**Verification.** Each slice is behavior-preserving and provable by build +
the `--render` harness on a representative circuit (pixel-compare before/after).
The gate-library slice is additionally covered by the embedded-gatedef load path
already exercised by the render harness.

**Why not blind:** the grouping in step 1 is a design decision (what becomes an
injected dependency vs. what stays global) that should be agreed before churning
175 call sites.

---

## D — Typed command / undo snapshots

**Current state.** Commands derive from `wxCommand` (`klsCommand`) and carry raw
`GUICircuit*` / `GUICanvas*` pointers. Undo state is stored ad hoc per command
(e.g. `cmdMoveGate` keeps `startX/startY/endX/endY`), and cross-session/clipboard
persistence goes through a **stringly-typed** `toString()` on each command with
the matching parser living in `klsClipboard.cpp` and `GUICanvas.cpp`. There is no
single typed representation of "what changed", and no round-trip test.

**Plan.**
1. Define a typed `CommandSnapshot` variant (one alternative per command kind)
   holding the fields each command already stores, replacing the free-form
   `toString()` format with serialization *from the typed snapshot*.
2. Move the string parser out of the canvas/clipboard into one
   `parseCommand(snapshot) -> klsCommand` factory next to the snapshot type.
3. Keep `Do()` / `Undo()` operating on the live model as they do now.

**Risk.** Every command and the clipboard/undo persistence format are touched;
a format regression silently breaks copy/paste and saved undo history.

**Verification.** The typed snapshot gives us the seam we lack today: a
**round-trip test** (`snapshot -> string -> snapshot`) that needs no GUI, plus a
factory test that a parsed snapshot reconstructs the same command fields.
`Do`/`Undo` against the live model still need the interactive harness.

**Why not blind:** the persistence format is user-visible (saved undo, clipboard
interop); changing it without agreeing on backward compatibility risks breaking
existing workflows.

---

## E — Threading model

**Current state.** The GUI↔logic split runs on `threadLogic` +
`autoSaveThread`, coordinated entirely through the singleton: a
`wxCriticalSection` (`m_critsect`), a `wxMutex` guarding the message deques
(`mexMessages`), a `wxMutex` for `wireStateBuffer` (`wireStateMutex`), and
semaphores (`m_semAllDone`, `simulate`, `readyToSend`). Locking is **inconsistent**:
most sites use RAII (`wxMutexLocker`, `wxCriticalSectionLocker`), but at least one
hand-rolls a spin-wait —
`while (wxGetApp().mexMessages.TryLock() == wxMUTEX_BUSY) wxYield();`
(`threadLogic.cpp:48`) — which is not equivalent to a plain blocking lock (the
`wxYield()` pumps the GUI event loop while waiting).

**Plan.**
1. Document the intended lock hierarchy and which data each lock protects.
2. Standardize on RAII lockers **only where provably equivalent**; the
   spin-plus-yield site is *not* equivalent and must be analyzed, not
   mechanically replaced.
3. Once C moves the queues/locks into a `MessageBus` object, give it a small,
   audited lock-safe API instead of exposing raw mutexes.

**Risk.** Highest of the three. Concurrency bugs are non-deterministic and can
survive into a shipped build; a "cleanup" that changes wait semantics can cause
deadlocks or UI stalls that no unit test will catch.

**Verification.** No differential test can prove this safe. It needs targeted
runtime stress (rapid edit-during-simulation, autosave under load) and review of
each lock-order change. **This is the one workstream to do interactively, in
small audited steps, not as an unattended PR.**

---

## Suggested order

C's gate-library slice first (unblocks a `MessageBus`/`GateLibrary` seam), then
D's typed snapshots (gains a real round-trip test), and E last and by hand once
its state lives behind C's objects.
