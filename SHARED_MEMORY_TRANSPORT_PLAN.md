# Shared-memory transport for sensor payloads — plan

Status: **phases 2 and 3 (clients) built**: Linux and macOS server; Python, Rust and C++ clients. On
the target Linux GPU machine, transport was more than 5% of total time, which was enough to go ahead.
The server half is `TempoCore/.../TempoSharedMemory.{h,cpp}`, whose header documents the wire layout.
The client halves are `tempo_sim/_shared_memory.py`, `tempo_sim::shared_memory` and the C++ client's
`tempo/shared_memory.h`. The Design section is
the original draft; [As built](#as-built) lists where the build departs from it, and why.

## Goal

Move large sensor payloads (images, depth, labels, lidar arrays) between the sim and a client on the
same machine without sending them through gRPC. The change should be an implementation detail of
`FTempoServer` and the generated clients:

- No `.proto` message changes and no new client-facing API. A scenario that streams color images today
  keeps doing so, unchanged, and gets the same message types back.
- Raw gRPC stubs, remote clients and anything that cannot map the memory keep working, with inline
  bytes as today.
- FixedStep determinism and write semantics (`FinishWrites`, `HasUnflushedWork`, stall handling) stay
  as they are.

## Why: where the time goes today

A frame's payload is copied about five times between readback and the client:

1. GPU readback into the proto's `std::string` (`TempoCamera.cpp`, `TempoLidar.cpp`). Needed in some form.
2. A copy of the whole response into `ResponseQueue` when a write is already in flight on that stream
   (`TempoServer.h`, `OnResponse`).
3. gRPC serializes it into its slices, once per stream: N clients on one camera mean N copies.
4. Two kernel copies, plus HTTP/2 framing and flow control. The Unix socket transport removes TCP, not these.
5. The client parses it into a new buffer. In Python this holds the GIL.

Shared memory removes 3–5, shares one buffer between subscribers, and later (phase 3) can remove 1.

Typical rig: eight cameras and a 256k-return lidar at 10 Hz.

| Cameras | Color only | Color, depth, labels |
|---|---|---|
| 540p | ~180 MB/s | ~400 MB/s |
| 1080p | ~550 MB/s | ~1.4 GB/s |

These rates are per second of sim time; running faster than real time multiplies them.

### First measurement (Mac, editor binaries in `-game`, Metal off-screen, OpenWorld map)

| 8 × 1080p + lidar | Sim speed | Frame p50 | Write wait / frame | Server sent | Write latency p50 / p95 |
|---|---|---|---|---|---|
| Color | 0.32× real time | 315 ms | 0.1 ms (0.0%) | 175 MB/s | 56 / 88 ms |
| Color, depth, labels | 0.26× real time | 388 ms | 0.5 ms (0.2%) | 354 MB/s | 146 / 181 ms |

Rendering dominated. Writes took a third of a frame to complete, but finished while the next frame
rendered, so FixedStep never waited on them. Transport becomes the limit once **frame time falls below
write latency**: roughly under 150–180 ms per frame for the full set on this client. A faster GPU
gets there.

## As built

These differ from the first draft of this plan:

- **No field option.** Every top-level singular `bytes` field of at least `Tempo.SharedMemory.MinBytes`
  (64 KiB) is eligible, in any streaming RPC, project ones included. That needs no `.proto` change and
  no new `Options.proto`. A field option would also have needed protobuf extension support across
  the shared `tempogrpc` library.
- **A handshake, then references that name their segment.** There is no stream token. The server
  makes the stream's first segment with its first message, names it in the initial metadata
  (`tempo-shm-segment`, `tempo-shm-size`), and sends payloads inline until the client has mapped it and
  set its `attached` word. After that, each 80-byte reference carries its segment's name and size, so
  a resize needs no announcement of its own. The client recognises a reference by its exact length
  and its `TEMPOREF` magic, and only on a stream that asked for shared memory.
- **Fields in a `oneof` stay inline**, proto3 `optional` included. prost folds a oneof into an enum, so
  a client could not reach the field without knowing the message.
- **The payload is copied into the slot when the response arrives** (`OnResponse`), not when it is
  written. What is written, or queued behind a write in flight, is the small envelope. This removes
  the queue copy for shared-memory streams without changing `TResponseDelegate`. The `MoveTemp`
  change would have broken every handler that binds a member function with a `const&` signature,
  and several sensors answer many streams from one response.
- **Names are unlinked once the client attaches.** The client sets the header's `attached` word after
  mapping a segment, and the server unlinks the name on its next message. Nothing is left in
  `/dev/shm` after that, even if either process crashes. So no PID-based cleanup sweep is needed.
- **Field kinds come from `FieldDescriptor::CopyTo`.** The inline `FieldDescriptor::type()` and
  `cpp_type()` go through an absl `call_once` whose spinlock `tempogrpc` does not export, so using
  them fails to link.
- **Linux reserves the segment's pages** with `posix_fallocate`. A container's `/dev/shm` is 64 MB by
  default, and writing a page tmpfs can't supply raises SIGBUS instead of returning an error.
- **A client that cannot map the announced segment just never attaches.** The server goes on sending
  inline, so nothing is lost and nothing reopens, and no later stream in that process asks for shared
  memory again. A first draft reopened the stream instead and lost the message in hand; that could
  not be done in the Rust and C++ wrappers without changing what they return.
- **The Rust and C++ wrappers keep their signatures.** Rust's calls `tonic::client::Grpc` directly,
  with a codec whose decoder restores the payloads, so it still returns `tonic::Streaming<T>`. C++'s
  `ServerStream<T>::read` restores them. Neither uses protobuf reflection (C++ links the same
  `tempogrpc`, so it would hit the same `call_once` link failure). Instead the generators emit, for
  each response type, the list of its eligible fields: a `SharedMemoryMessage` impl in Rust, and a
  lambda in C++.
- **Slot release ordering:** Rust and C++ free a slot with a release store. Python uses a plain 4-byte
  store, which is ordered after the copy on x86, but only in practice on ARM.

Measured on the Mac (render-bound, so sim speed doesn't move; full 1080p rig, Python client):

| | Write wait / frame | gRPC `Write` total | Payload via shared memory |
|---|---|---|---|
| `TEMPO_SHARED_MEMORY=0` | 5.7 ms (p95 15.4) | 0.67 s | none |
| shared memory | 0.3 ms (p95 0.4) | 0.09 s | 7.3 GB in 1,431 messages, none inline |

Write latency barely moved (p50 123 ms off, 115 ms on). A write's completion is seen only when the
game thread next polls the completion queue, so on a 380 ms frame that number mostly measures the
polling interval.

## Decision gate

Run `write_wait.py` on the target Linux GPU machine, for both stream sets, over TCP and the Unix socket.

- **Write wait share above ~10% of a frame**, or a sim speed limited by write latency: build it.
- **Client received falls behind server sent**: the client is the bottleneck. Measure a Rust client
  before concluding anything about gRPC; part of the gain may come from Python parsing alone.
- **Write wait share near zero**: don't build it yet. Do the cheap fixes below and revisit when rigs or
  GPUs grow.

## Cheap fixes, independent of shared memory

- **Move, don't copy, into `ResponseQueue`.** `OnResponse` takes `const ResponseType&` and enqueues a
  copy. Let the response delegate take the response by value (or add an rvalue overload) so a sensor
  can `MoveTemp` its message in. It saved 10–20% of writes from a full-frame copy in the first run.
- **Encode once for N subscribers.** Several streams on one sensor each serialize the same message.
  Serializing once into a `grpc::ByteBuffer` and writing that (through `grpc::GenericServerAsyncWriter`,
  or a `SerializationTraits` specialization for a pre-serialized wrapper) removes copy 3 per extra
  subscriber. This also works over the network.

## Design

### Overview

gRPC stays the control plane and the per-message envelope. Every response is still a gRPC message on
the same stream, in the same order, with the same backpressure. On a stream that negotiated shared
memory, a large `bytes` field carries a small reference to a slot in a shared ring instead of the
payload. The generated client wrapper swaps the payload back in before the caller sees the message.

```
sensor ─► FTempoServer::Respond ──► copy payload into ring slot ──► Write(envelope + slot ref)
                                                                        │ gRPC stream
client wrapper ◄── read slot, put bytes back, mark slot released ◄──────┘
```

### 1. Marking eligible fields

A custom field option in a new `TempoCore/Options.proto`:

```proto
extend google.protobuf.FieldOptions { bool shared_memory = 51234; }  // number to be registered
```

applied to the payload fields:

```proto
bytes depths_m = 4 [(TempoCore.shared_memory) = true];
```

Only top-level `bytes` fields may carry it (checked by `gen_protos.py`). Messages and the wire format
are unchanged for everyone who doesn't negotiate. Candidates: `ColorImage.data`, `DepthImage.depths_m`,
`LabelImage.data`, every per-return array in `LidarScanSegment`. `VideoFrame.data` probably not: it is
already compressed and small.

### 2. Negotiation

A generated wrapper that can map memory sends request metadata `tempo-shm: 1` on a streaming call. The
server answers in the stream's initial metadata, before the first message:

- `tempo-shm-segment`: the segment's name
- `tempo-shm-token`: a random 128-bit stream token
- `tempo-shm-layout`: version, slot count, slot size

If the server can't or won't create a segment (Windows in phase 1, a remote peer, the client opted out),
it sends nothing back, and the stream carries inline bytes as today. If the client fails to map the
segment, it cancels and reopens the stream without the header. It never guesses.

The server only offers shared memory to a peer on the same host: a Unix domain socket or a loopback
address, per `grpc::ServerContext::peer()`.

### 3. The reference lives in the field, not in an unknown field

The obvious place for a slot reference is a reserved unknown field, but **prost (the Rust client) drops
unknown fields**. So on a negotiated stream, a marked field holds a fixed 48-byte `SlotRef` in place of
its payload:

```
token[16] | segment generation u32 | slot u32 | offset u64 | length u64 | sequence u64
```

The wrapper recognises it by exact length and the stream's token. A real payload that is exactly 48
bytes and starts with a random 128-bit token is not a practical risk. When a payload is small (below
`Tempo.SharedMemory.MinBytes`, e.g. 64 KiB), the server sends it inline, and the wrapper tells the two
apart the same way.

### 4. The ring

One segment per negotiated stream. It holds a header and N slots (default 4, `Tempo.SharedMemory.Slots`),
each sized for the stream's largest message so far:

```
header: magic, version, generation, slot_count, slot_size,
        per slot: atomic state (FREE / WRITTEN / READING), sequence
slots:  slot_count × slot_size bytes
```

- **Server:** in `TRequestManager<Streaming>::Respond`, for a negotiated stream, take a FREE slot, copy
  each marked field into it, build the envelope (section 5), mark the slot WRITTEN with release
  ordering, then `Write` the envelope.
- **Client wrapper:** on receiving an envelope, check token, generation and sequence, copy the bytes
  out, then mark the slot FREE with release ordering.
- **No free slot:** send that message inline. The server never blocks on the ring, so a slow client
  degrades to today's behavior instead of deadlocking the game thread. Counted in `Tempo.WriteStats`.
- **Resize** (a camera's resolution changes): allocate a new segment with a bumped generation, and
  announce it in the first envelope that uses it with an extra `SlotRef` field (`generation` changes,
  `segment` name in a trailing variant). The client maps it and unmaps the old one.
- **Cleanup:** the server unlinks the segment when the stream finishes or is cancelled (`Close`), and
  at startup removes leftover `tempo-shm-*` segments whose owning PID is gone.

Ordering needs no extra signalling: the client only reads a slot after receiving the envelope, which
the server sends after writing the slot, and a socket send and receive between processes orders memory.
The atomics are there for slot reuse.

**Write semantics are unchanged.** A write still completes when gRPC accepts the envelope, so
`HasUnflushedWork`, `FinishWrites`, the FixedStep drain and `Tempo.WriteStallTimeoutMs` mean what they
mean today. Slot release is decoupled from write completion on purpose: a client that holds slots
gets inline payloads, not a stalled sim.

### 5. Building the envelope without copying the payload

`Respond` receives a `const ResponseType&`, so the payload can't be cleared in place. Phase 1 builds
the envelope by reflection: copy every unmarked field (headers and sizes, a few hundred bytes), set each
marked field to its `SlotRef`, and copy the payload once, into the slot. The field descriptors for each
message type are found once and cached. Together with the `MoveTemp` fix above, the whole server path
copies the payload once, into shared memory.

### 6. Clients

All three generated clients learn the same small runtime, in their hand-written parts (`tempo_sim`,
`tempo_sim::`, the C++ client's runtime), and the generators call it only for streams whose response
type has marked fields:

- **Python:** `multiprocessing.shared_memory` / `mmap`. Copy-once into the message's `bytes`, which is
  fully transparent. A later, opt-in zero-copy API can return a `memoryview` that holds its slot until
  released.
- **Rust:** `memmap2`. Copy-once into the message's `Vec<u8>` / `Bytes`.
- **C++:** `mmap` / `MapViewOfFile`. Copy-once into the message's `std::string`.

`TEMPO_SHARED_MEMORY=0` turns it off in every client, for debugging and A/B measurement.

### 7. Platforms

- **Server:** `FPlatformMemory::MapNamedSharedMemoryRegion` covers Linux, Mac and Windows.
- **Linux:** POSIX shm under `/dev/shm`. A client in a container needs `--ipc=host` or a shared
  `/dev/shm` mount; without it, mapping fails and negotiation falls back to inline. A later option is
  `memfd_create` plus passing the fd over the Unix socket (`SCM_RIGHTS`), which needs no shared
  namespace and leaves no names to clean up.
- **macOS:** shm names are limited to 31 characters, so use `tempo-shm-<short id>`.
- **Windows:** named file mappings. Phase 2; the Unix-socket-only peer check doesn't apply there, so it
  needs a loopback check.
- **Permissions:** segments are created mode 0600 and named with random ids.

## Why not iceoryx

Considered, not recommended for this design:

- **What iceoryx provides is a messaging system**: discovery, pub/sub, notification, flow control,
  lifecycle. Here gRPC already does all of that, along with the step-and-write semantics FixedStep
  depends on. Two event systems would have to be reconciled with `FinishWrites` and steps between
  frames.
- **What's left is small**: a slot ring per stream and a mapping helper in three clients, a few hundred
  lines with no new dependency.
- **Classic iceoryx** needs the RouDi daemon running beside the sim.
- **iceoryx2** has no daemon, but it is Rust. It would have to become a TempoThirdParty prebuilt for every
  platform, and all three clients would need its bindings. The maturity of its Python bindings is
  unverified.

Revisit iceoryx2 if the goal grows into removing gRPC from the data path entirely (sub-millisecond
latency, many subscribers, a shared bus other tools read). ROS 2 users can already get shared memory
through their RMW without Tempo's involvement.

## Phases

1. **Cheap fixes.** Encode once for N subscribers (not done). For shared-memory streams the `MoveTemp`
   fix is superseded: their payload is copied once, into the slot.
2. **Done: shared memory, Linux and macOS, Python first.** Field option, negotiation, ring, reflection
   envelope, Python runtime and codegen, `Tempo.WriteStats` counters for slot hits and inline fallbacks,
   and a `write_wait.py` A/B (`TEMPO_SHARED_MEMORY=0/1`).
3. **Done: Rust and C++ clients.** Then Windows.
4. **Write straight into the slot.** A `FTempoServer` payload-allocation call lets readback decode into
   the slot directly, removing copy 1. Sensor code changes here, not before.
5. **Optional zero-copy client APIs** (`memoryview`, borrowed slices) for clients that want them.

## Tests

Built: `Tempo.Sensors.SharedMemory.*` (handshake, round trip, ring full goes inline, resize and
unlinking); unit tests of the Rust reader (`cargo test --lib shared_memory`); `Tests/Rust/examples/shared_memory.rs`
and `Tests/Cpp/shared_memory.cc`, which compare a camera's captures over shared memory and through a
raw stub against a rendering sim;
`Tests/Python/test_shared_memory.py` (the client reader, against a segment written by the test, in
the `contract` group); and in the `sensors` group, color and lidar streams over shared memory compared
byte for byte with a raw-stub stream of the same captures. Still to write: a cancelled stream unlinks
its segment, and a resize seen end to end from a client.

The original list:

- **`codegen` group:** marked fields are found and validated, and the generated wrappers call the runtime.
- **Python and Rust `integration` groups, shared memory on and off:**
  - payloads are byte-identical to the inline path
  - a slow consumer falls back to inline without a stall
  - a resize mid-stream switches segments
  - a cancelled stream unlinks its segment
- **A raw-stub client** keeps receiving inline payloads.
- **C++ unit tests** (`Tempo.Core.SharedMemory.*`) for the ring's slot state machine and `SlotRef`
  encoding.

## Open questions

- One segment per stream, or per client connection with slots shared between that client's streams?
  Per stream is simpler; per connection means fewer mappings with many sensors.
- Should several subscribers to one sensor share one slot (reference-counted) instead of each stream
  having its own ring? That's the shared-memory form of "encode once", and it saves memory bandwidth
  with many clients.
- The default slot count, and whether it should scale with sensor rate.
- Should the server's offer be gated on `Tempo.SharedMemory` (default on) for the transition?
