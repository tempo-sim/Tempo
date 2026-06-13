# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Measure Tempo gRPC transport overhead against a *live* Tempo sim — TCP vs UDS.

Latency: many calls to a tiny RPC (`tempo_core.get_sim_time`) on each transport, reporting
p50/p95/p99/mean.

Throughput: subscribes to a sensor stream on each transport, counts messages and bytes for
a fixed duration, and reports MB/s and msg/s.

Run against a running Tempo sim (editor PIE or packaged) — the sim dual-listens on TCP and
UDS by default, so this script just flips the client side. For a CI-runnable workload that
doesn't need a sim, see `Tools/TransportBenchmark` (the standalone echo server + client).

Examples:
    # Default: both modes, both transports (1080p color stream for 10s throughput)
    python ExampleClients/Python/TransportBenchmark.py

    # Latency only, 5000 iters
    python ExampleClients/Python/TransportBenchmark.py --mode latency --latency-iters 5000

    # Throughput against a specific lidar
    python ExampleClients/Python/TransportBenchmark.py --mode throughput \\
        --sensor-kind lidar --sensor-owner Vehicle --sensor-name LidarFront --throughput-seconds 10
"""

import argparse
import asyncio
import statistics
import time
from dataclasses import dataclass
from typing import Optional

import tempo_sim
import tempo_sim.tempo_core as tc
import tempo_sim.tempo_sensors as ts
import tempo_sim.TempoSensors.Sensors_pb2 as Sensors_pb2


# --------------------------------------------------------------------------------------
# Transport switching
# --------------------------------------------------------------------------------------

async def set_transport(transport: str, port: int):
    """Switch the client between transports. Resets any cached channel."""
    if transport == "tcp":
        await tempo_sim.set_server(address="localhost", port=port)
    elif transport == "uds":
        await tempo_sim.set_unix_socket(port=port)
    else:
        raise ValueError(f"unknown transport: {transport}")


# --------------------------------------------------------------------------------------
# Latency
# --------------------------------------------------------------------------------------

@dataclass
class LatencyStats:
    transport: str
    iters: int
    p50_us: float
    p95_us: float
    p99_us: float
    mean_us: float


async def run_latency(transport: str, port: int, iters: int) -> LatencyStats:
    await set_transport(transport, port)
    # Warm-up: 20 calls to stabilize the channel + first-use codegen.
    for _ in range(20):
        await tc._get_sim_time()  # use the async wrapper directly

    samples = []
    for _ in range(iters):
        t0 = time.perf_counter_ns()
        await tc._get_sim_time()
        samples.append(time.perf_counter_ns() - t0)
    samples.sort()
    def p(q):
        return samples[min(int(iters * q), iters - 1)]
    stats = LatencyStats(
        transport=transport,
        iters=iters,
        p50_us=p(0.50) / 1000.0,
        p95_us=p(0.95) / 1000.0,
        p99_us=p(0.99) / 1000.0,
        mean_us=statistics.fmean(samples) / 1000.0,
    )
    print(f"[latency {transport}] iters={iters}  "
          f"p50={stats.p50_us:.1f}us  p95={stats.p95_us:.1f}us  "
          f"p99={stats.p99_us:.1f}us  mean={stats.mean_us:.1f}us")
    return stats


# --------------------------------------------------------------------------------------
# Throughput
# --------------------------------------------------------------------------------------

@dataclass
class ThroughputStats:
    transport: str
    msgs: int
    bytes_total: int
    elapsed_s: float
    mbps: float
    msgs_per_s: float


_KIND_TO_TYPE = {
    "color": Sensors_pb2.MT_COLOR_IMAGE,
    "depth": Sensors_pb2.MT_DEPTH_IMAGE,
    "label": Sensors_pb2.MT_LABEL_IMAGE,
    "lidar": Sensors_pb2.MT_LIDAR_SCAN,
}


async def _resolve_sensor(kind: str, owner: Optional[str], name: Optional[str]):
    """Find a sensor of the requested measurement type, preferring user-supplied owner/name."""
    want = _KIND_TO_TYPE[kind]
    avail = await ts._get_available_sensors()
    for s in avail.available_sensors:
        if owner and s.owner != owner:
            continue
        if name and s.name != name:
            continue
        if want in s.measurement_types:
            return s.owner, s.name
    return None


async def run_throughput(transport: str, port: int, kind: str,
                         owner: Optional[str], name: Optional[str],
                         duration_s: float) -> Optional[ThroughputStats]:
    await set_transport(transport, port)
    resolved = await _resolve_sensor(kind, owner, name)
    if resolved is None:
        print(f"[throughput {transport}] no {kind} sensor found "
              f"(owner={owner}, name={name}); skipping.")
        return None
    sensor_owner, sensor_name = resolved
    print(f"[throughput {transport}] streaming {kind} from {sensor_owner}:{sensor_name} for {duration_s}s")

    stream_fns = {
        "color": ts._stream_color_images,
        "depth": ts._stream_depth_images,
        "label": ts._stream_label_images,
        "lidar": ts._stream_lidar_scans,
    }
    stream_fn = stream_fns[kind]

    msgs = 0
    bytes_total = 0
    start = time.perf_counter()
    deadline = start + duration_s
    # All generated stream wrappers take (owner, sensor); lidar adds include_color.
    if kind == "lidar":
        stream = stream_fn(sensor_owner, sensor_name, False)
    else:
        stream = stream_fn(sensor_owner, sensor_name)
    async for msg in stream:
        msgs += 1
        bytes_total += _payload_bytes(kind, msg)
        if time.perf_counter() >= deadline:
            break
    elapsed = time.perf_counter() - start
    stats = ThroughputStats(
        transport=transport,
        msgs=msgs,
        bytes_total=bytes_total,
        elapsed_s=elapsed,
        mbps=(bytes_total / 1e6) / elapsed if elapsed > 0 else 0.0,
        msgs_per_s=msgs / elapsed if elapsed > 0 else 0.0,
    )
    print(f"[throughput {transport}] msgs={stats.msgs}  bytes={stats.bytes_total}  "
          f"elapsed={stats.elapsed_s:.2f}s  {stats.mbps:.1f} MB/s  {stats.msgs_per_s:.1f} msg/s")
    return stats


def _payload_bytes(kind: str, msg) -> int:
    """Approximate per-message bytes for a sensor message — useful for MB/s figures."""
    if kind == "color":
        return len(msg.data)
    if kind == "label":
        return len(msg.data)
    if kind == "depth":
        # repeated float depths_m, 4 bytes each
        return len(msg.depths_m) * 4
    if kind == "lidar":
        # rough: 5 repeated float arrays (distances, intensities, azimuths, elevations) + colors + reflectivities
        return (len(msg.distances_m) * 4 + len(msg.intensities) * 4
                + len(msg.azimuths_rad) * 4 + len(msg.elevations_rad) * 4
                + len(msg.colors) + len(msg.reflectivities))
    return 0


# --------------------------------------------------------------------------------------
# Comparison output
# --------------------------------------------------------------------------------------

def print_latency_compare(tcp: LatencyStats, uds: LatencyStats):
    speedup = tcp.p50_us / uds.p50_us if uds.p50_us > 0 else float("inf")
    print("\n=== Latency: TCP vs UDS ===")
    print("              p50 (us)   p95 (us)   p99 (us)   mean (us)")
    print(f"  TCP        {tcp.p50_us:>9.1f}  {tcp.p95_us:>9.1f}  {tcp.p99_us:>9.1f}  {tcp.mean_us:>9.1f}")
    print(f"  UDS        {uds.p50_us:>9.1f}  {uds.p95_us:>9.1f}  {uds.p99_us:>9.1f}  {uds.mean_us:>9.1f}")
    print(f"  UDS p50 speedup vs TCP: {speedup:.2f}x")


def print_throughput_compare(tcp: ThroughputStats, uds: ThroughputStats):
    speedup = uds.mbps / tcp.mbps if tcp.mbps > 0 else float("inf")
    print("\n=== Throughput: TCP vs UDS ===")
    print("              MB/s        msg/s       elapsed (s)")
    print(f"  TCP        {tcp.mbps:>9.1f}  {tcp.msgs_per_s:>9.1f}  {tcp.elapsed_s:>11.3f}")
    print(f"  UDS        {uds.mbps:>9.1f}  {uds.msgs_per_s:>9.1f}  {uds.elapsed_s:>11.3f}")
    print(f"  UDS MB/s speedup vs TCP: {speedup:.2f}x")


# --------------------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------------------

async def main():
    parser = argparse.ArgumentParser(
        description="Benchmark Tempo gRPC transport overhead (TCP vs UDS) against a live sim.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--port", type=int, default=10001,
                        help="Tempo server port (controls TCP port and UDS path suffix).")
    parser.add_argument("--transport", choices=["tcp", "uds", "both"], default="both",
                        help="Which transport(s) to exercise.")
    parser.add_argument("--mode", choices=["latency", "throughput", "both"], default="both",
                        help="Which benchmark(s) to run.")

    # Latency knobs.
    parser.add_argument("--latency-iters", type=int, default=2000)

    # Throughput knobs.
    parser.add_argument("--sensor-kind", choices=["color", "depth", "label", "lidar"],
                        default="color")
    parser.add_argument("--sensor-owner", default=None,
                        help="If unset, picks the first sensor with the requested kind.")
    parser.add_argument("--sensor-name", default=None)
    parser.add_argument("--throughput-seconds", type=float, default=5.0)

    args = parser.parse_args()

    transports = ["tcp", "uds"] if args.transport == "both" else [args.transport]
    modes = ["latency", "throughput"] if args.mode == "both" else [args.mode]

    latency_results = {}
    throughput_results = {}

    for transport in transports:
        if "latency" in modes:
            latency_results[transport] = await run_latency(
                transport, args.port, args.latency_iters)
        if "throughput" in modes:
            throughput_results[transport] = await run_throughput(
                transport, args.port, args.sensor_kind,
                args.sensor_owner, args.sensor_name, args.throughput_seconds)

    if "tcp" in latency_results and "uds" in latency_results:
        print_latency_compare(latency_results["tcp"], latency_results["uds"])
    if (throughput_results.get("tcp") is not None
            and throughput_results.get("uds") is not None):
        print_throughput_compare(throughput_results["tcp"], throughput_results["uds"])


if __name__ == "__main__":
    asyncio.run(main())
