# No test opens an error dialog

Branch `test-guard` off origin/master (4ac66aa). The work happens in a
worktree of its own. This plan is an untracked file of the owner's tree
and is not in the worktree; the brief names it by its full path.

The owner asked on 2026-10-01 that nothing be compiled for this change
while he uses the PC: the branch is delivered unbuilt, and he builds and
runs the tests himself. The code below was compiled before that, as a
probe outside the repository (see Checks).

## The problem

The tests are debug builds. A debug program that asserts or aborts opens
a "Microsoft Visual C++ Runtime Library" dialog and waits for a click. On
2026-09-29 such dialogs came up on the owner's screen from test programs.
`ctest` has no time limit in this project, so a test that waits for a
click holds `ctest` for good.

Since `tidy-ups`, `LogTest` switches the dialogs off in its own `main()`.
The other twelve tests do not.

## The fix

Every test switches the debug runtime's dialogs off before its `main()`
runs: the reports go to stderr, the process ends by itself with an exit
code that is not 0, and `ctest` shows the test as failed. The calls are
the ones `LogTest` makes today.

They go into a file of its own, which `add_bot_test()` builds into every
test. All 13 tests get it, the ones to come too, whatever they include.
Not into `tests/Check.h`: `TimeTextTest` does not include it and cannot,
because its own `failures` would clash with the one in `Check.h`.

## Files

1. `tests/QuietFailures.cpp`, new, exactly:
   ```cpp
   #include <crtdbg.h>
   #include <cstdlib>

   namespace {

   // An assertion or an abort must not wait for a click: ctest has no time limit here.
   struct QuietFailures
   {
       QuietFailures()
       {
           _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
           _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
           _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
           _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
           _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
           _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
           _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
       }
   };

   const QuietFailures quietFailures;

   }
   ```
   The comment is `LogTest`'s, moved unchanged. No header and no `#ifdef`.

2. `tests/CMakeLists.txt`: in `add_bot_test()`, the line
   `    add_executable(${name} ${ARGN})` becomes
   `    add_executable(${name} QuietFailures.cpp ${ARGN})`.
   It stands first in the list: MSVC runs the static constructors of a
   test's objects in link order, and CMake links them in list order, so
   the guard runs before the test's other static objects. Nothing else in
   the file changes.

3. `tests/LogTest.cpp`: the comment, the seven calls and the blank line
   after them go from the start of `main()`, which then begins with
   `logging::nameThisThread("main");`. `#include <crtdbg.h>` and
   `#include <cstdlib>` go too: both were there for those calls only.

`README.md` and the top-level `CMakeLists.txt` do not change.

## Known, and left as it is

- A crash (a null pointer, a stack overflow) is not covered: it is
  Windows Error Reporting that would show a window, and whether it does
  was not measured.
- A failure in the start-up code of a DLL that a test loads (dpp, Qt)
  runs before the guard.
- The file is for MSVC. A Linux build would leave it out of the list in
  `add_bot_test()`; nothing is written for that now.
- If the tests are ever linked from a static library, the linker drops
  an object nothing refers to, and the guard with it.

## Rules

Comments only where really necessary and really short (the why, never the
what). Code that reads like its surroundings. C++17. Commit messages =
short header + `*` bullets, one line each, ending with the coding model's
co-author trailer. Nothing pushed. Nothing added or changed under `docs/`.
`token.json` is never read, printed, copied or touched. The bot is never
started, stopped or killed. History is linear: no merge commits.

Nothing is compiled, built or run for this change: no compiler, no ninja,
no cmake, no ctest, no probe. The commit message claims nothing about a
build or a test run of the branch.

One commit with three files, staged by name: `tests/QuietFailures.cpp`,
`tests/CMakeLists.txt`, `tests/LogTest.cpp`. After the review the
orchestrator removes the worktree, so that the branch is checked out
nowhere.

## Checks

- Done before the code was written, by the plan's check, outside the
  repository: `QuietFailures.cpp` as above, built into `QueueTextTest`
  and `TimeTextTest` with the repository's flags, gave no warning; its
  constructor ran before `main()` in both; both tests passed; a small
  CMake project with the changed `add_bot_test()` line configured, built
  and ran under `ctest`; `LogTest.cpp` without the lines of item 3
  compiled without a warning.
- Not done: what a failing test does with the guard was not measured for
  this change, because provoking the dialogs made sounds while the owner
  used the PC. The same calls were measured on 2026-09-29: a program that
  asserts and aborts ended by itself with exit code 3 and no window.
- Owner's part, when he has the PC free: build, then `ctest` twice: 13
  tests pass both times.
