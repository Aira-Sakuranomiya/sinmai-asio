# Native adapter profile and invariants

This is an experimental, hash-pinned adapter, not a general CRI SDK binding.
Only the exact `game` fingerprints in `dependencies.json` are accepted. Addresses below are RVAs,
resolved against the loaded module, never absolute addresses in the game process.

| RVA / layout | Verified use |
| --- | --- |
| `0x73e50` | Native backend registration setter; original bytes `48 89 0d 29 3f 11 00 c3` |
| `0x7cca0` | Main ASR render entry, disabled on the original scheduler in ASIO mode |
| `0x187d80` | Currently selected platform backend interface |
| `0x117bb0` | WASAPI backend interface table |
| `0x126e50` | Built-in Pseudo backend interface table |
| `0x187f48` | ASR rack table pointer; element 0 is the main output rack |
| `0x1602d8` | Unity Atom initialized flag |
| `0x16a88` / `0x16b24` | Atom lock / unlock wrappers |
| `0x2eca0` | Exported `criAtomAsr_SetOutputDataCallback` |
| `0x86b74` | Consume packet bytes per planar channel, advance channel pointers |
| `0x86cf0` | Return a packet to its owning queue |

The adapter copies all **38** interface entries from the Pseudo table (0x130 bytes), retains allocation/init/finalize,
initial data submission (slot 13) and channel/rate setup, and replaces begin (slot 12), normal submission
(slot 14), active query (slot 16), and queued-latency query (slot 35). The latter reports current ring frames
plus driver-reported output latency in samples. Copying only the initial methods is unsafe: normal music
playback queries the tail of this interface. Only a WASAPI registration is substituted. A different registration
is a conflict. The setter is detoured with pinned MinHook 1.3.4; disk game files remain untouched.

Main rack output wrapper is at rack +8 and consists of `{interface, context}`. The Pseudo context's first
32-bit value is the actual output channel count. Rack +0x80 is the per-iteration frame budget, set to the
requested block size under the Atom lock after initialization. Rack +0xa8 is the existing output callback;
it must be null before this adapter registers its timing callback.

Packets have owner queue at +8, return-list index at +0x10, allocated channel capacity at +0x11,
remaining bytes per channel at +0x24, and planar channel pointers at +0x28. Capacity is eight even when
active output is stereo. Successfully consuming all bytes transfers ownership back to the queue; merely
returning null without recycling would exhaust the renderer. Unconsumed bytes must stay in CRI's queue.

The output callback is `void(void* user, int channels, int frames, float** planar)`. It records generation
time, cadence and frame counts without calling back into Atom. Submission timestamps retain the last
generation time for the one outstanding output packet, including time spent in CRI's partial packet.

CRI server processing can migrate between OS threads. The producer is logically single and must not
overlap. An acquire/release atomic flag rejects overlap without waiting; migration itself is counted,
not treated as a fault. The ASIO callback is the sole consumer. Its fixed-capacity ring is exactly the
requested block size; all format conversion is bounded and allocation-free.

Capture mode uses the original CRI scheduler and discards packets after recording them. Normal/clock
mode detours the main render entry as well as backend registration. The original CRI scheduler performs
no render work on this path; additional racks are a fault. An ASIO callback atomically consumes a complete
block, then signals a native MMCSS Pro Audio producer. That producer enters the Atom lock and calls the
original render trampoline with a one-block budget. The realtime callback never waits for this work.
There is one initial primed block and no growing queue, resampling stage or secondary output clock.

The ASIO worker owns the COM apartment and driver lifecycle. Realtime callbacks only fill buffers and
update atomic counters; faults are stopped by the worker. The Mono layer binds explicit `GetProcAddress`
exports from a full-path `LoadLibraryW` handle, avoiding Unity Mono's nested-directory P/Invoke lookup.

Driver creation is deferred to the pre-Atom-initialization hook. An early Yamaha driver load can occupy
the game's fixed native base 0x180000000. The pinned game module does not tolerate that relocation;
its presence at the expected base is checked before creating the ASIO COM instance.

The software queue/callback measurements do not prove physical latency, event alignment or 30-minute
gameplay stability. See `validation.md` for measured evidence and remaining gates.

## Driver preferred buffer (ABI 3)

SaInitialize accepts frames=0 for automatic selection and returns the effective frame count in the same options struct on success. The ASIO worker sets 48 kHz before querying getBufferSize, selects its preferred size, validates both the plugin capacity and driver constraints, then resets the one-block ring before creating buffers. Capture-only probes have no ASIO driver and use 128 frames when automatic.

The managed layer captures SetConfigParameters arguments after configuration and replays them through Harmony before Atom initialization, once the safely deferred ASIO initialization reports its buffer size. Only the output rate, channel count and server frequency are overridden; the captured remaining parameters and other mods' patches are retained. An automatic request temporarily uses 128 for the initial pre-initialization config call and replaces it with the actual size before any CRI rendering starts. Driver changes during playback still require a game restart.

## Stereo peak protection (ABI 4)

Before final ASIO encoding, each stereo frame passes a memoryless linked soft knee. Finite peak p <= 0.90 is unchanged; above it, the target peak is 0.90 + 0.08 * tanh((p - 0.90) / 0.08). A common target/p gain preserves stereo ratios and bounds output near 0.98. Non-finite input samples are individually replaced by zero. The encoder retains its defensive clamp. No lookahead, history, queue or delay line is added; near-full-scale material is intentionally changed and this cannot repair upstream clipping or underruns.

SaStats appends inputPeak, overRangeFrames, limitedFrames and invalidSamples; size is 1248 bytes, while SaOptions remains 280. Native and managed must both use ABI 4. The callback aggregates metering per block and publishes lock-free atomic counters; diagnostics retain the existing five-second cadence. Capture-only mode has no output callback and does not meter these output-stage values.

## ASIO timing diagnostics (ABI 5)

Stats is now 1392 bytes with timing fields appended after the ABI 4 peak meter. Each active callback records QPC entry spacing and flags intervals strictly greater than 1.5x / 2x frames/48000. The first callback is excluded from gap comparison; startup gaps otherwise remain included. Driver ASIOTime samplePosition is consumed only when kSamplePositionValid is set. Missing metadata breaks the comparison chain. Legacy callbacks count as unavailable; no getSamplePosition call is added. A 64-bit position assembled from hi/lo preserves low-word rollover. Position discontinuities and callback spacing are evidence, not physical xrun measurements.

Actual resync/reset/buffer-change/latency-change messages increment independent atomics before the existing fault path. Capability queries do not increment event counts. Output, rendering, buffering and recovery policies are unchanged. Metrics are published through the existing five-second managed diagnostics; no realtime file access or logging is added.

## Output PCM capture (ABI 6)

SaConfigureCapture(30) is called only before playback when the opt-in CaptureOutput setting is true. It allocates two banks of 30 seconds, in each output channel's actual encoded ASIO format. After filling the driver buffers and before outputReady, the callback copies their bytes into the active bank. There is no driver query, managed call, allocation, file access or wait on this path. Saving requests a bank handoff; the next active callback publishes the frozen bank with release/acquire synchronization, then keeps recording in the other bank. Only the non-realtime saver decodes the frozen bank into a stereo float32 RIFF/WAVE file with fact and data chunks. This preserves sample values to float32 precision, not original encoded byte representation.

A managed pool worker invokes SaSaveCapture with a unique output path. The native capture mutex serializes configure/save/stop, never callback writes. Saving waits at most one second for an active callback and returns an error if none acknowledges; no fault is latched. A pending handoff remains valid if a later callback arrives. File open/write errors release the frozen bank. Native stop waits for the saver before stopping the stream; memory stays owned by the module. Callback bank histories reset at handoff, so successive files can be shorter than 30 seconds. The marker is polled at the normal five-second cadence. Capture is not automatically saved at exit and is not a physical loopback.

## Superseded experiment: positive short-render handling (0.1.8, ABI 6)

Captured final PCM contained 638 stereo all-zero runs of at least three samples, all ending on a 256-frame boundary, including repeated 71/72-sample gaps. The pinned platform wrapper at RVA 0x7cca0 calls the ASR mixer at 0x2d450; if its result is smaller than the request, it zero-fills the tail and replaces the valid length with the requested length. The adapter then counts those zeros as supplied data, so source-underrun counters remain zero.

A guarded in-memory five-byte branch edit at 0x7cd57 changes 41 3B C7 73 3C (cmp eax,r15d; jae 0x7cd98) to 85 C0 75 3D 90 (test eax,eax; jne 0x7cd98; nop). Nonzero actual lengths bypass padding and flow through the wrapper's existing packet-length/progress logic. The loop continues under its original budget; aligned extra samples stay in its existing partial packet. A zero result still takes the original silence fallback, avoiding zero-length callback faults or a non-progressing render loop. The surrounding call/mov/cmp signature and full DLL hash are checked before applying; the original bytes are restored only after CRI finalization. Installation failure rolls back the branch and prepared hooks. No disk bytes or second render clock are introduced.

Known-profile tests copy the actual branch bytes into an ABI-correct executable harness and check partial, full and zero-result behavior before/after installation and restoration. These checks confirm instruction behavior and reversibility, not runtime audio correctness.

## Small mixer quantum experiment (0.1.9, ABI 6)

The 0.1.8 follow-up capture still showed the same block-aligned zero tails. The installed DLL hashes matched and live read-only verification found the intended patched bytes, while all submitted blocks remained 256 frames. This rejects the wrapper's positive-short-result padding path as the direct explanation for this reproduction. That branch edit is removed; its original zero-fill and result-length handling is retained.

The single-variable 0.1.9 experiment changes only the existing maximum mixer quantum at RVA 0x7cce5 from mov r15d,256 (41 BF 00 01 00 00) to mov r15d,32 (41 BF 20 00 00 00). The original loop explicitly rounds requests to 32-sample alignment, selects the lesser of the remaining budget and this maximum, and already supports partial submission. ASIO buffer size, serverFrequency, one-block queue capacity and timing ownership remain unchanged. Hash/signature guards, pre-init application, post-finalization restoration and executable instruction tests are retained. More internal mixer calls may increase producer work; actual audio behavior and performance remain unverified.
