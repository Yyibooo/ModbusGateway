from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import argparse
import re
import select
import socket
import statistics
import time

from gateway_disconnect_demo import (
    GATEWAY_ENV,
    TIMEOUT, gateway_variant, receive_frame, start_gateway,
)
from gateway_system_demo import (
    available_port, expected_values, serial_pair, serve_rtu_read, tcp_request,
)

CLIENTS = 4
DEVICE_DELAY = 0.020


def serve_batch(serial):
    for _ in range(CLIENTS):
        serve_rtu_read(serial, {1: 123}, delay=DEVICE_DELAY)


def exchange(peers, burst):
    latencies = []
    pending = {}
    started = time.monotonic()
    for transaction, peer in enumerate(peers, 1):
        pending[peer] = (transaction, time.monotonic())
        peer.sendall(tcp_request(transaction, 1, 3, 0, 1))
        if not burst:
            assert receive_frame(peer) == expected_values(transaction, (123,))
            latencies.append(time.monotonic() - pending[peer][1])

    if burst:
        while pending:
            readable, _, _ = select.select(list(pending), [], [], TIMEOUT)
            assert readable, "等待并发回复超时"
            for peer in readable:
                transaction, sent = pending.pop(peer)
                assert receive_frame(peer) == expected_values(transaction, (123,))
                latencies.append(time.monotonic() - sent)
    return latencies, time.monotonic() - started


def run_batch(executable, server, client, burst):
    port = available_port("127.0.0.1")
    env = {**GATEWAY_ENV, "MBGW_LISTEN_PORT": str(port), "MBGW_QUEUE_CAPACITY": str(CLIENTS),
           "MBGW_QUEUE_TIMEOUT_MS": "0", "MBGW_RESPONSE_TIMEOUT_MS": "500"}
    with server.open("r+b", buffering=0) as serial, start_gateway(executable, client, env=env, port=port, verbose=False) as gateway:
        with ExitStack() as stack, ThreadPoolExecutor(max_workers=1) as worker:
            peers = [stack.enter_context(socket.create_connection(("127.0.0.1", port), TIMEOUT))
                     for _ in range(CLIENTS)]
            device = worker.submit(serve_batch, serial)
            latencies, duration = exchange(peers, burst)
            device.result(timeout=TIMEOUT)
        assert gateway.wait(timeout=TIMEOUT) == 0
        logs = gateway.stdout.read().decode()
        timings = [(int(queued), int(forwarded)) for queued, forwarded in
                   re.findall(r"排队=(\d+) ms 转发=(\d+) ms", logs)]
        assert len(timings) == CLIENTS and "完成=4 失败=0" in logs, logs
    return latencies, duration, timings


def measure(executable, rounds, burst):
    latencies = []
    timings = []
    duration = 0
    with serial_pair() as (server, client):
        for _ in range(rounds):
            batch, elapsed, logs = run_batch(executable, server, client, burst)
            latencies.extend(value * 1000 for value in batch)
            timings.extend(logs)
            duration += elapsed
    queued, forwarded = zip(*timings)
    p95 = statistics.quantiles(latencies, n=100, method="inclusive")[94]
    label = "burst" if burst else "serial"
    print(f"{label}: n={len(latencies)}, throughput={len(latencies) / duration:.1f} req/s, "
          f"RTT p50={statistics.median(latencies):.1f} ms, p95={p95:.1f} ms, "
          f"queue p50={statistics.median(queued):.1f} ms, queue max={max(queued)} ms, "
          f"forward p50={statistics.median(forwarded):.1f} ms")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="对比单请求和并发请求的 RTU 软件链路耗时")
    parser.add_argument("--rounds", type=int, default=25)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds 必须是正整数")
    with gateway_variant(CLIENTS) as executable:
        measure(executable, args.rounds, burst=False)
        measure(executable, args.rounds, burst=True)
