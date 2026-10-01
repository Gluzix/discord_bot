# A "Playing:" announcement that came back without a message

Branch `announce-failure` off master (88bae82), created in the owner's
tree, which is on master and clean.

The plan went through its check on 2026-09-29. The check made it smaller:
one branch in one function, and one rules bullet.

## The problem

On 2026-09-28 the bot's requests to Discord timed out. DPP logged
`HTTP(S) error ... Timed out while waiting for the response`, and minutes
later the bot logged `A "Playing:" message has no id to edit - its
buttons, if any, stay`. The bot itself said nothing at the moment the
announcement failed.

The cause is in DPP 10.1.6: `confirmation_callback_t::is_error()` reads
the HTTP status and the body only. A request that timed out has status 0
and an empty body, so `is_error()` answers false, and the callback gets an
empty `dpp::message` (id 0) as a success. `PlayingPanel::announced()`
believes it and stores the id 0.

DPP writes that "Timed out" line only when the connection was never made,
so on that day the message did not reach Discord. There is a second way
to an empty message, which DPP does not log at all: Discord answers 200,
and the body is lost on the way. The message is in the channel then, with
its buttons, and the bot does not know its id.

## The fix

`announced()` takes an answer without a message id for a failure: it logs
a warning and stores nothing. That covers both ways, because both hand
out an empty message. Nothing else changes: the song plays on, and a
message the bot cannot name keeps its buttons, as today.

The warning does not say that the message was not sent: in the second
case it was.

YAGNI:
- `retire()` and its callback stay as they are. The edit takes nothing
  from its answer, and DPP logs its timeout itself, as for every other
  request of the bot.
- The existing `is_error()` branch of `announced()` stays as it is.
- No retry of the announcement, no helper function, no test target (the
  unit needs a live cluster; the reviewers check the premise with a probe
  against the real dpp.dll instead).
- When DPP never calls back (in 10.1.6: the connection cannot even be
  started, or the cluster shuts down), `announced()` does not run; DPP's
  own error line and the debug line of `retire()` stay the only trace.

## Files

1. `Helpers/PlayingPanel.cpp`, `announced()` only. After the `is_error()`
   branch, which stays as it is:
   ```cpp
   const uint64_t id = static_cast<uint64_t>(answer.get<dpp::message>().id);
   if (id == 0) {
       qWarning() << "No id came back for the \"Playing:\" message - its buttons, if any, stay";
       return;
   }
   messageId = id;
   ```
   No comment beside it: the rules block says why.

2. `Helpers/PlayingPanel.h`: one more bullet in the rules block, after the
   second one (`retire() finds no message id ...`):
   ```cpp
   // - announced() takes an answer without a message id for a failure:
   //   dpp 10.1.6's is_error() passes a request that timed out as a success,
   //   with an empty message.
   ```
   Nothing else in the header changes.

3. `README.md`: nothing. `CMakeLists.txt`: nothing.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class header, each bullet naming the function that enforces it. Code
that reads like its surroundings. C++17. Commit messages = short header +
`*` bullets, one line each, ending with the coding model's co-author
trailer. The commit message names the DPP defect, as the README asks
("Bugs that live in DPP itself are documented in commit messages").
Nothing pushed. Nothing added or changed under `docs/`. `token.json` is
never read, printed or touched. The bot is never started, stopped or
killed. DPP untouched. Zero warnings. History is linear: no merge commits.

One commit. Files are staged by name, never with `git add -A` or
`git commit -a`: `Helpers/PlayingPanel.h`, `Helpers/PlayingPanel.cpp`.

## Checks

- Build everything (bot and tests) in the agents' own build directory,
  which is configured on this tree; zero warnings in the ninja output;
  `ctest`: all 12 tests pass. Never `cmake -B build` in the tree, and
  never the owner's Qt Creator directory.
- A probe against the real dpp.dll, outside the repository, builds the
  answer as DPP does (`dpp::confirmation_callback_t answer(nullptr,
  dpp::message().fill_from_json(&j), http)` with an empty `j`):
  - status 0, empty body, `error = h_connection` (the timeout):
    `is_error()` is false and the message id is 0;
  - status 200, empty body, `error = h_success` (the body was lost): the
    same;
  - status 200 and a body with an id: `is_error()` is false and the id is
    that id.
- Owner's runtime pass: play a song, the "Playing:" message comes and its
  buttons go when the song ends, as before. The failure itself cannot be
  provoked on purpose; the warning shows in the log the next time an
  announcement comes back without its message.
