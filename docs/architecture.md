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
