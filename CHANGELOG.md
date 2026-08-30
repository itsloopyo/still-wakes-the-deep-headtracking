# Changelog

## [0.1.0] - 2026-08-30

### Other

- Hello world

## [0.0.0] - 2026-08-29

### Added

- Added a field-of-view setting, `[Camera] FovOffset`. The game ships without one, so the view is whatever Habitat picked. The value is degrees added to the FOV the engine is about to render with, written into the view info the render path builds, so the game's own FOV changes are kept and menus and cutscenes stay at the framing the game chose. It changes only what is drawn - what you can reach and grab is untouched - and the reticle correction follows the same value.
- Added the torch's glare to what the beam carries, so it is no longer left behind when the beam follows your head. The card that draws it hangs off the torch body rather than off the beam, so aiming the beam with the head left the glare pinned to the world; it now rides the beam, picking up the beam's own sway in the process. `[Torch] FlareFollowsBeam=0` leaves it where the game put it.
- Added head-aimed torch control, so the torch points where you are looking rather than where you are aiming. `[Torch] Multiplier` scales the head pose the beam is given (default `1.5`, so the beam leads the view; `1.0` moves it with the view exactly), and `[Torch] Enabled=0` turns it off. It is added to the aim the game gives the torch's own spring arm, so nothing else in the game moves with it.
- Added cutscene detection, reading the flag the game's own `GetCutsceneMode()` returns off the player controller the camera hook already holds - so it follows whatever Habitat counts as a cutscene rather than guessing from the camera. The menu gate never saw one, because the mouse cursor stays hidden throughout a cutscene. Head tracking keeps running during a cutscene, so you can look around in one; the FOV offset is the only thing that stands down, leaving the shot at the focal length it was composed for.
- Added reticle and interaction-prompt placement on the interaction ray, instead of at the centre of the head-tracked picture. Both are the game's own widgets, so they keep their artwork and their targeted-object states; the rest of the HUD stays put.
- Added the mod version to the log's opening line, so an attached `HeadTracking.log` says which build wrote it.
- Initial release.

### Changed

- Changed the mod to apply the tracker pose as absolute, keeping no centre of its own. Every tracker app centres itself, so a mod-side centre sat in series with the tracker's and the two drifted apart. Centre in your tracker app instead. The recentre hotkey (Home / Ctrl+Shift+T) is gone with it.
- Capped the per-hook `hook #...` detail line at 20 samples, instead of logging every 2 seconds for the whole session (~400 KB an hour). The 30-second heartbeat still reports liveness, tracker data and inject mode.
- Capped the `reticle: offset ...` line at 20 samples on the same 2-second interval. It was counted in pushes, which arrive at frame rate, so on a fast machine it wrote a line every couple of seconds for the whole session: 200 of the 275 lines in a seven-minute test run were that one line saying the reticle was still following the aim.
- Kept one previous generation of the log. It already started fresh on every launch; the session before a crash is now kept as `HeadTracking.prev.log` next to the game exe, and `uninstall.cmd` removes it.
- Split smoothing into two `[Rotation]` keys: `LocalSmoothing` (default `0.0`, tracker running on this PC) and `RemoteSmoothing` (default `0.15`, tracker on a remote network device). The value is picked per connection from the packet source address and covers both rotation and position.

### Fixed

- Fixed a tracker port held by another game not being picked up when it frees up. Launching with a previous game still running left the mod unable to bind UDP 4242; it now waits on the port and binds within about a second of the other game closing, with no relaunch and nothing to press. An out-of-range `[Network] UdpPort` falls back to 4242 and says so, rather than truncating to a wrong port that nothing ever sends to. The heartbeat line carries `udpPort=` so a log distinguishes waiting on the port from waiting on packets.
- Fixed the reticle stopping following the aim ray part way through a session. Changing level rebuilds the HUD, and the widget the mod was moving is destroyed with it, but a destroyed UObject's memory keeps its class pointer, so the mod's liveness test kept reporting the dead widget as fine and never went looking for its replacement. Liveness is now settled against the engine's own object array, and the targets are re-walked periodically, so the reticle re-attaches after a level change instead of sitting at screen centre for the rest of the session.
- Fixed the widget walk picking a Blueprint template instead of the widget on screen. Each target name exists twice while the game runs, once in the loaded asset and once as the widget the HUD built, and both carried the widget Blueprint's name in their outer chain. The reticle and tooltip are now matched against the HUD actor that only exists at runtime.
- Fixed head sway moving the camera the opposite way to the head. The tracker's sideways channel arrives mirrored relative to its yaw channel, so leaning or turning slid the view to the wrong side; it is now negated at the engine boundary alongside the forward/back channel.
- Fixed menus and pause depending on reading the desktop's cursor. The gameplay gate reads `APlayerController::bShowMouseCursor` off the controller directly, so tracking suppresses on this game's menu state rather than on whatever any other window did with the cursor.

### Removed

- Removed `[Rotation] Smoothing` and `[Position] Smoothing`.
- Removed the hidden 0.15 baseline smoothing floor, so a local tracker now gets zero-latency, unsmoothed tracking by default.
