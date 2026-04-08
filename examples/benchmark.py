#!/usr/bin/env python3
"""Benchmark script for the io_uring coroutine HTTP server."""

import argparse
import multiprocessing
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import Request, urlopen

DEFAULT_URL = "http://localhost:8080/"
DEFAULT_CONNECTIONS = 64
DEFAULT_REQUESTS = 10000
SERVER_BIN = "build/linux/x86_64/release/http_server"


def start_server(port: int) -> subprocess.Popen:
    proc = subprocess.Popen(
        [f"./{SERVER_BIN}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    time.sleep(0.5)
    if proc.poll() is not None:
        print(f"server failed to start: {proc.stdout.read().decode()}")
        sys.exit(1)
    print(f"server started (pid={proc.pid}) on port {port}")
    return proc


def stop_server(proc: subprocess.Popen) -> None:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    print("server stopped")


def send_request(url: str) -> float:
    start = time.monotonic()
    req = Request(url, method="GET")
    with urlopen(req, timeout=10) as resp:
        resp.read()
    return time.monotonic() - start


def run_benchmark(url: str, total_requests: int, connections: int):
    latencies: list[float] = []
    errors = 0

    print(f"\nbenchmark: {total_requests} requests, {connections} concurrent connections")
    print(f"url: {url}\n")

    wall_start = time.monotonic()

    with ThreadPoolExecutor(max_workers=connections) as pool:
        futures = [pool.submit(send_request, url) for _ in range(total_requests)]
        for future in as_completed(futures):
            try:
                lat = future.result()
                latencies.append(lat)
            except Exception as e:
                errors += 1

    wall_elapsed = time.monotonic() - wall_start

    if not latencies:
        print(f"all requests failed ({errors} errors)")
        return

    latencies.sort()
    n = len(latencies)
    qps = n / wall_elapsed

    def percentile(p: float) -> float:
        idx = min(int(n * p / 100), n - 1)
        return latencies[idx]

    print(f"{'='*50}")
    print(f"  requests:    {n} (errors: {errors})")
    print(f"  elapsed:     {wall_elapsed:.2f} s")
    print(f"  QPS:         {qps:.0f} req/s")
    print(f"{'='*50}")
    print(f"  latency (ms):")
    print(f"    min:    {latencies[0]*1000:.2f}")
    print(f"    avg:    {statistics.mean(latencies)*1000:.2f}")
    print(f"    median: {latencies[n//2]*1000:.2f}")
    print(f"    P90:    {percentile(90)*1000:.2f}")
    print(f"    P95:    {percentile(95)*1000:.2f}")
    print(f"    P99:    {percentile(99)*1000:.2f}")
    print(f"    max:    {latencies[-1]*1000:.2f}")
    print(f"{'='*50}")


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark the io_uring coroutine HTTP server")
    parser.add_argument("-n", "--requests", type=int,
                        default=DEFAULT_REQUESTS,
                        help=f"total requests (default: {DEFAULT_REQUESTS})")
    parser.add_argument("-c", "--connections", type=int,
                        default=DEFAULT_CONNECTIONS,
                        help=f"concurrent connections (default: {DEFAULT_CONNECTIONS})")
    parser.add_argument("--url", default=DEFAULT_URL,
                        help=f"target URL (default: {DEFAULT_URL})")
    parser.add_argument("--no-server", action="store_true",
                        help="skip server start/stop (server already running)")
    parser.add_argument("--port", type=int, default=8080,
                        help="server port (default: 8080)")
    args = parser.parse_args()

    proc = None
    if not args.no_server:
        proc = start_server(args.port)

    try:
        run_benchmark(args.url, args.requests, args.connections)
    finally:
        if proc:
            stop_server(proc)


if __name__ == "__main__":
    main()
