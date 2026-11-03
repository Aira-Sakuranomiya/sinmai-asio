# Validation record

Status: **experimental implementation accepted by the user at 128 samples** on 2026-10-27.
The user reported that a dense chart played without problems, then ended testing and closed the game.
No 30-minute completion or physical latency result is claimed.

Version 0.1.1 removes obsolete gate instructions, defaults to 128 samples with diagnostics disabled,
reduces normal logging to startup/errors, and reports underruns in plain language without repeated error
spam. The native audio engine is unchanged; its executable section matches the playback-tested 0.1.0
engine byte for byte. Build, audio unit tests and profile safety tests were rerun; the game was not restarted
for the logging cleanup. Developer capture/clock tools remain optional and inactive in ordinary launches.

## Environment

- Sinmai 1.70, MelonLoader 0.6.4, Unity 2018.4.7f1, Windows x64.
- Yamaha URX22C; Yamaha Steinberg USB ASIO, 48 kHz, Low Latency.
- Driver initially fixed its supported buffer to 256 samples. The official control panel was changed
  to 64 for investigation and finally 128 for the accepted run. This setting persists outside the game;
  original value was 256. The plugin configuration is also 128. The user's speaker routing is preserved.
- Visual Studio 2026 MSVC x64 and Windows SDK 10.0.26100 components were installed to complete the
  previously partial native build toolchain.

## Completed checks

- Native and managed builds; .NET Framework 4.7.2 targeting references; C/C++17 static CRT.
- PCM conversion clipping, negative/full-scale, non-finite input, float32/64, packed 24-bit byte order.
- Stereo ring partial writes, full/empty boundaries, wraparound, and one million concurrent frames.
- Standalone 64-sample ASIO silence: 5.026 seconds, 3,621 callbacks, 231,744 output frames, zero faults,
  no source underruns, maximum observed callback work 97.3 us. Driver-reported output latency 135 samples
  / 2.8125 ms. Driver start time is included in elapsed time, so this snapshot is not a sample-rate drift test.
- Negative probes: invalid 65-sample buffer, nonexistent driver, and duplicate output channel all failed
  explicitly with exit code 1, without starting output.
- ABI rejection preserves the stats destination buffer; a deliberately unsupported native fixture is
  rejected by SHA256 before internal-pointer access or hooking. A known CRI profile accepts a reversible
  registration hook and restores the original entry bytes/backend slot exactly.
- Fresh capture: 30.79 seconds, 1,475,520 stereo frames, nonzero PCM, zero faults, WASAPI bypassed.
- ASIO-demand clock mode: 161.51 seconds with one 64-frame missing block in the aggregate, bounded
  queue and continuous music PCM. It exited normally with code 0. This was a functional gate, not a
  zero-underrun 64-sample stability pass.
- Actual 64-sample output was not stable. A later run recorded 448 missing samples after warmup and
  stopped with fault 1. Maximum observed Atom lock wait was 1,988.5 us, above the 1,333 us block deadline.
- Accepted 128-sample snapshot: 667.94 seconds, 32,051,968 ASIO output frames, 32,052,096 accepted CRI
  frames (the 128-frame difference is the prepared next block), 29,233,755 nonzero output frames, zero
  missing samples including startup, zero faults, queue peak 128, one producer thread.
- In that 128-sample snapshot, generation-to-ASIO-copy median upper bound was 2.75 ms and P99 upper
  bound 3.0 ms; driver-reported output latency was 247 samples / 5.1458 ms. Maximum callback work was
  180 us, render work 179.1 us, and Atom-lock wait 1,365.3 us. These are separate software/driver metrics.
- Normal game shutdown was tested multiple times, including active ASIO clock/playback, with exit code 0.

## Bugs identified and fixed during validation

- Unity Mono did not resolve the nested native module through filename-only P/Invoke: explicit export binding.
- Packet channel capacity was confused with active output channels: distinct layout checks.
- Consumed CRI packets require queue recycling; initialization data must retain the original submission method.
- CRI can migrate serialized work between threads: reject concurrent producers rather than thread changes.
- Early ASIO loading relocated `amdaemon_api.dll` and crashed startup: defer driver initialization.
- Copying only the interface prefix left tail queries invalid and crashed music startup: retain all 38 methods.
- A software-timed CRI producer starved a one-block ASIO queue: switch rendering to hardware demand.

## Remaining limits / checks not completed

- The planned 30-minute soak was ended early at the user's request after dense-chart acceptance.
- 32 samples, other devices/game versions, and all possible seek/scene transitions are not certified.
- Physical device disconnect/reset behavior was not exercised. Unknown game-assembly and output-callback
  conflict guards were not exercised in-game; unknown CRI rejection was verified with a native fixture.
- Physical/event-to-audio latency comparison with shared mode and a reference exclusive device.

`UserData/SinmaiAsio.stats.json` holds the latest aggregate; `tools/summarize-stats.ps1` summarizes it.
Histogram overflow bucket 63 is a lower bound, not a precise 16 ms measurement. All reported software
waiting and driver latency must stay labeled separately from physical/acoustic latency.

## 0.1.2 configuration and build-layout validation

- First-load configuration tests passed: single/preferred driver selection, missing/ambiguous driver handling, TOML escaping, existing disabled/custom settings preservation, and malformed settings rejection without overwrite.
- Native audio and CRI profile/hook restoration tests passed after moving developer outputs to .build/. Runtime build/ contains exactly SinmaiAsio.dll and SinmaiAsio.Native.dll.
- These changes were built and tested outside the game. The first-load path has not been exercised under Unity Mono in-game; installed game files and existing settings were left unchanged.

## 0.1.4 automatic buffer validation

- Full native and managed build passed. Offline tests cover preferred/manual size selection, unsupported preferences, ring wraparound and stereo ordering at all 13 supported sizes, automatic config defaults, and ABI 3 capture-mode effective-size reporting. Existing conversion/concurrency and known/unknown CRI hook safety checks passed.
- The pinned managed CRI IL was inspected: SetConfigParameters forwards the configuration to native code; InitializeLibrary uses that stored configuration. The plugin replays the captured configuration with the effective server frequency before native Atom initialization.
- No ASIO driver was opened and no game was launched for this change. Preferred-size discovery against UR22C, Harmony replay under Unity Mono, and playback stability still require in-game verification.

## 0.1.5 output peak protection validation

- Native/managed build and all offline tests passed. Soft-knee checks cover unchanged low-level samples, continuity near the knee, monotonic output, positive/negative symmetry, linked stereo gain, extreme finite peaks, NaN/Infinity sanitation, and no processing history after a transient.
- Native static assertions and managed size/offset checks agree on ABI 4: Options 280 bytes, Stats 1248 bytes, InputPeak offset 1216, InvalidSamples offset 1240. Existing buffer, conversion, concurrency, configuration, and known/unknown CRI hook tests passed.
- Metering is cumulative at the ASIO output stage, before soft limiting. No ASIO callback timing measurement, game playback, installed DLL replacement or sound-card settings changes were performed for this version. It remains unverified whether the reported pops are caused by clipping or whether this processing improves them.

## 0.1.6 ASIO timing diagnostics validation

- Full native/managed build passed. Timing tests verify strict 1.5x and 2x thresholds, excess interval calculation and invalid input guards. Position tests cover initial baseline, normal advance, forward gaps, duplicate/backward/short advances, chain reset on unavailable metadata, and a 64-bit position crossing the low-word rollover with a 192-frame buffer.
- ABI 5 layout checks passed: Options 280 bytes; Stats 1392 bytes; CallbackMaxGapUs offset 1248; LatencyChangeRequests offset 1384. Capture-mode exported stats start with zero timing/event counts. All existing limiter, conversion, buffering, concurrency, config and known/unknown CRI tests passed.
- Source inspection confirmed capability queries return before driver-event counters, legacy callbacks report position unavailable, time-info callbacks require kSamplePositionValid, and no getSamplePosition query was added. Driver recovery/output policy is unchanged. No game, ASIO driver or physical playback test was run for this change; the reported crackles remain unresolved pending a new diagnostic capture.

## 0.1.7 output capture validation

- Full native/managed build and offline tests passed. Capture tests cover supported output-format decoding, wrapping and oversized writes, chronological WAV sample order, RIFF/fact/data sizes, frozen-bank stability while the other bank is repeatedly overwritten, repeated handoffs, disabled capture rejection and invalid formats. Configuration tests cover default-off capture and opt-in parsing. ABI 6 retains the 1392-byte Stats layout.
- Callback inspection confirms that capture copies final encoded output bytes before outputReady and never writes files or waits for the saver. Capture failures use the ordinary diagnostic error return, not the audio fault latch. Capture APIs with a live ASIO driver, marker processing under Unity Mono, disk failure handling and audible reproduction still require game verification. No driver test tone or game launch was performed during these build checks.

## 0.1.7 reproduction and 0.1.8 candidate fix

- A user-triggered 30-second, 48 kHz stereo capture during the reported crackles contained 638 exact-zero runs >=3 frames ending on 256-frame boundaries. Runs of 71/72 frames occurred 191/99 times. One example dropped from about -0.70 to zero for 71 frames and resumed around -0.74 at the next boundary. Capture peak was 0.979895; companion stats reported zero source underruns, zero invalid samples, zero position discontinuities/resync events and an earlier single large callback gap. Captures are local diagnostics and are not included in releases.
- 0.1.8 is a candidate fix for the verified wrapper padding path. Full build and offline tests passed. The known-CRI test verifies applied/restored instruction bytes and executes the actual branch bytes in a small protected-code harness for short/full/zero output lengths. Unknown CRI rejection and existing tests passed. The game has not yet been replayed with the fix; stable playback, musical timing and disappearance of the boundary gaps remain to be verified.

## 0.1.8 reproduction and 0.1.9 controlled experiment

- Follow-up 0.1.8 capture again contained 1363 stereo exact-zero runs >=3 frames, all ending on 256-frame boundaries (614 of length 71, 357 of length 72). Log/stats confirmed 0.1.8, zero peak limiting/over-range frames and zero source underruns/position discontinuities. Both installed DLL hashes matched build outputs. A read-only inspection of the loaded CRI branch returned 85-C0-75-3D-90, proving that the intended 0.1.8 edit was active. Submitted PCM blocks nevertheless remained exactly 256 frames; the earlier diagnosis of that specific wrapper padding path was premature.
- The ineffective branch edit was removed. 0.1.9 changes only the internal maximum mixer quantum to 32 while retaining the 256-frame ASIO setting and its existing CRI server frequency. Full build and offline tests passed; the known-profile test executes the actual quantum-setting instruction before installation (256), after installation (32) and after restoration (256). Unknown-profile rejection and other existing tests passed. This remains a controlled diagnostic experiment, not a confirmed fix.

## 0.2.0 version and observed playback status

- Version 0.2.0 retains the 0.1.9 implementation and ABI 6; the version change does not alter audio processing. Live 0.1.9 startup logs confirmed a 256-frame ASIO buffer and 32-frame CRI submissions. The available snapshot covered about 50 seconds with zero source underruns, zero position discontinuities and no fault; the user subsequently reported no further crackle reproduction so far.
- Diagnostics and capture were then disabled in the user's runtime config. A fixed-version 30-minute soak, post-change PCM comparison and additional sound-card verification are still incomplete. Absence of a reported recurrence is not a measured guarantee that the defect is eliminated.
