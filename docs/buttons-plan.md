# Buttons on the "Playing:" message

Branch `playing-buttons` off master. The "Playing:" announcement gets one row
of buttons; a click is routed into the existing command objects. No new
commands, no duplicated policy: the same permission rule, the same replies.

## Facts that shape it (DPP 10.1.6)

- A click is its own interaction (`cluster::on_button_click`,
  `dpp::button_click_t : interaction_create_t`, field `custom_id`). It must be
  answered with `event.reply(...)` within Discord's 3 s, like a slash command.
  `get_parameter` is private on `button_click_t` - the compiler forbids
  reading slash parameters from a click.
- `slashcommand_t` and `button_click_t` share `interaction_create_t`: `from()`,
  `command.guild_id`, `command.channel_id`, `command.get_issuing_user()`,
  `command.type` (`dpp::it_component_button` for a click), `reply(...)`.
- Buttons live in an action row: `dpp::component().set_type(dpp::cot_action_row)
  .add_component(dpp::component().set_label(..).set_style(..).set_id(..))`,
  attached with `message::add_component`. Ephemeral reply:
  `dpp::message(text).set_flags(dpp::m_ephemeral)`.

## Behaviour

- Row under every "Playing:" message (both the edited "Looking for your
  song..." reply and the fresh message of a queued song): `⏪ 10s`, `⏯`,
  `⏩ 10s`, `⏭ Skip`. Loop replays in song mode announce nothing, so they get no
  buttons; queue-mode replays announce and do.
- A click acts on the song playing NOW, whatever message it was clicked on -
  the way every music bot's control bar works. No song id in the button: it
  would break song-loop replays (a replay gets a new id but no new message)
  and buys nothing a user wants.
- `⏯` is one button: it pauses when playing, resumes when paused.
- A click is answered on the message it sits on (Discord's UPDATE_MESSAGE
  response): the command's normal reply text becomes a status line under the
  title, replaced on every click, and the row stays where it is - nothing new
  is posted, so the panel never scrolls away. (First version posted ephemeral
  replies; ten clicks pushed the row off screen.) A refusal ("You need to be
  in my voice channel...") and the unknown-button answer stay ephemeral: they
  concern only the clicker. A click when nothing plays puts the command's own
  "Nothing is playing right now!" on the status line.
- A click on a button the bot no longer knows (older message format after a
  future change) gets an ephemeral `messages::unknownButton`.
- Every click is logged like a slash command: `button forward:10 received
  N ms after it was issued` / `button forward:10 handled in N ms`.

## Files

1. `Helpers/PlaybackButtons.h/.cpp` (next to Labels - the user-facing layer;
   `Playback/DecoderWorker.cpp` may include it, `Commands/` too):
   ```cpp
   namespace buttons {
   // The row under a "Playing:" message.
   dpp::component controlRow();

   struct Click { std::string command; std::string argument; }; // "forward:10" -> {forward, 10}
   std::optional<Click> parse(const std::string &customId);
   }
   ```
   custom ids: `rewind:10`, `playpause`, `forward:10`, `skip`. `controlRow`
   and `parse` are the only two places that know the format. Emoji: the
   sources are plain ASCII, so write the glyphs as UTF-8 escapes in
   `set_emoji`: ⏪ `"\xE2\x8F\xAA"`, ⏯ `"\xE2\x8F\xAF"`, ⏩ `"\xE2\x8F\xA9"`,
   ⏭ `"\xE2\x8F\xAD"`; labels `10s`, none, `10s`, `Skip`; `cos_secondary`
   for all but `⏯` (`cos_primary`). If Discord rejects an emoji-only button,
   fall back to a label - say so.

2. `Commands/ICommand.h`: a second entry point,
   `virtual void execute(const dpp::button_click_t &event, const std::string &argument) = 0;`
   (forward-declare `dpp::button_click_t`). `Command` implements the default:
   ephemeral `messages::unknownButton` - a command without a button.

3. `Commands/Command.h/.cpp`:
   - `userMayControl` takes `const dpp::interaction_create_t &` (it only uses
     `from()`, `command.guild_id` and the issuing user). Every slash caller
     still compiles - `slashcommand_t` converts. `userMaySummon` and
     `VoiceConnector::ensureJoined` stay on `slashcommand_t` (/play, /join
     have no buttons).
   - `VoiceConnector::userInBotChannel` / `botIsAloneInChannel` take
     `const dpp::interaction_create_t &` for the same reason (check their
     bodies: `find_guild`, `from()->get_voice`, `get_issuing_user`, `owner->me`
     are all on the base type).
   - Replies go through `Helpers/Interactions`: `interactions::reply` (the
     result: a channel reply for a slash command, an UPDATE_MESSAGE status
     line for a click) and `interactions::refuse` (ephemeral for a click).
     `Command::reply` forwards to the former; `userMayControl` uses the latter.
   - (Superseded by the above - first version:) New protected helper, the ONE place that decides ephemerality:
     ```cpp
     // A click answers only the clicker; a slash command answers the channel.
     static void reply(const dpp::interaction_create_t &event, const std::string &text);
     ```
     `dpp::message msg(text); if (event.command.type == dpp::it_component_button) msg.set_flags(dpp::m_ephemeral); event.reply(msg);`
     `userMayControl`'s refusal goes through it.

4. The five button commands split parsing from doing. `execute(slash)` reads
   its parameters exactly as today and calls `run`; `execute(click, argument)`
   parses the argument string and calls the same `run`. All replies through
   `Command::reply`.
   - `PauseCommand`, `ResumeCommand`: `void run(const dpp::interaction_create_t &event);` - no argument; the click overload ignores it.
   - `ForwardCommand`, `RewindCommand`: `void run(const dpp::interaction_create_t &event, int seconds);` - the click parses `argument` with `std::stoi` guarded (`try`/`catch` or `std::from_chars`), clamped 1..600 like the slash path; an unparsable argument means the default 10.
   - `SkipCommand`: `void run(const dpp::interaction_create_t &event, size_t count);` - the click passes 1.
   No behaviour change for the slash path.

5. `Commands/ButtonRouter.h/.cpp` - the one class that receives every click
   and forwards it (the owner asked for it as a class):
   ```cpp
   class ButtonRouter
   {
   public:
       ButtonRouter(const std::unordered_map<std::string, std::unique_ptr<ICommand>> &commands_,
                    std::shared_ptr<PlaybackController> playback_);
       void handle(const dpp::button_click_t &event);
   private: ...
   };
   ```
   `handle`: log "received"; `buttons::parse(custom_id)`; unknown ->
   ephemeral `messages::unknownButton` + log; `playpause` -> the command is
   `resume` when `playback->isPaused()` else `pause`; look the command up in
   the map; `execute(event, argument)`; log "handled in". The router holds a
   reference to `CommandHandler`'s map (the handler outlives it - it owns
   both) and `playback` only to resolve the toggle; it never calls playback
   for the work itself.

6. `Playback/PlaybackController.h/.cpp`: `bool isPaused();` - via
   `clientIfSongInProgress()` like `pause()`/`resume()`: true only when a song
   is in progress and the client says `is_paused()`.

7. `Commands/CommandHandler`: owns a `std::unique_ptr<ButtonRouter>` created
   in `prepare()` after `setupCommands()`; in `setupBot()`:
   `bot->on_button_click([this](const dpp::button_click_t &event) { buttonRouter->handle(event); });`
   `interactionAgeMs` becomes `interactions::ageMs(const dpp::interaction_create_t &)`
   in a tiny `Helpers/Interactions.h/.cpp` shared by the handler and the
   router (move, don't copy).

8. `Playback/DecoderWorker.cpp`: in the announcement block,
   `nowPlaying.add_component(buttons::controlRow());` before `notifyUser`.

9. `Messages.h`: `unknownButton = "That button doesn't work any more - use the slash commands!"`.

10. `CMakeLists.txt`: the new files.

## Rules

Comments only where really necessary and really short (the why, never the
what); code that reads like its surroundings; commit messages = short header
+ `*` bullets, one line each, ending with the coding model's co-author
trailer; nothing pushed; `docs/` stays out of git; DPP untouched; zero
warnings. Suggested commits: (1) the command-base split (ICommand overload,
`reply`, `userMayControl`/`VoiceConnector` on the base type, the five
commands' `run`) - pure refactor, no behaviour change; (2) `PlaybackButtons`,
`ButtonRouter`, `isPaused`, `Interactions`, the wiring and the row on the
message.

## Checks

- Build after each commit, zero warnings; the bot is not running.
- A tiny harness in the scratchpad for `buttons::parse` (the four ids, an
  empty string, garbage, `forward:` with no number) - never in the repo.
- Owner's runtime pass: play a song and see the row; click each button; click
  `⏯` twice; click while nothing plays; click from outside the bot's channel
  with someone else in it (refusal, ephemeral); `/forward 30` still works;
  click on an older "Playing:" message (acts on the current song).
