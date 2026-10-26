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
