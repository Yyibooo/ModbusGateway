from contextlib import contextmanager
import select
import signal
import socket
import time

from gateway_disconnect_demo import (
    GATEWAY_ENV,
    GATEWAY,
    TIMEOUT,
    connect_client,
    gateway_variant,
    receive_frame,
    request,
    start_gateway,
    wait_for_log,
)


@contextmanager
def backend_listener():
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.settimeout(TIMEOUT)
        listener.bind(("127.0.0.1", 1502))
        listener.listen(1)
        yield listener


def check_shutdown(gateway, started, cancelled):
    assert gateway.wait(timeout=TIMEOUT) == 0
    elapsed_ms = (time.monotonic() - started) * 1000
    logs = gateway.stdout.read().decode()
    assert "收到退出信号" in logs, logs
    assert f"停止接入，取消排队请求 {cancelled} 条" in logs, logs
    print(logs, end="")
    print(f"网关以状态 0 退出，耗时约 {elapsed_ms:.0f} ms")


def run_idle_case(with_clients):
    with backend_listener(), start_gateway(GATEWAY) as gateway:
        if with_clients:
            with connect_client() as a, connect_client() as b:
                wait_for_log(gateway, "已接入")
                wait_for_log(gateway, "已接入")
                started = time.monotonic()
                gateway.send_signal(signal.SIGINT)
                assert a.recv(1) == b.recv(1) == b""
                check_shutdown(gateway, started, cancelled=0)
        else:
            started = time.monotonic()
            gateway.send_signal(signal.SIGTERM)
            check_shutdown(gateway, started, cancelled=0)


def run_pending_case(executable):
    env = {**GATEWAY_ENV, "MBGW_QUEUE_CAPACITY": "1"}
    with backend_listener() as listener, start_gateway(executable, env=env) as gateway:
        backend, _ = listener.accept()
        with backend, connect_client() as a, connect_client() as b, connect_client() as c:
            backend.settimeout(TIMEOUT)
            a.sendall(request(0))
            first = receive_frame(backend)
            assert first[7:] == request(0)[7:], first.hex()

            write = bytes.fromhex("00 01 00 00 00 06 FF 06 00 01 03 E7")
            b.sendall(write)
            c.sendall(write)
            readable, _, _ = select.select([b, c], [], [], TIMEOUT)
            assert len(readable) == 1
            exception = receive_frame(readable[0])
            assert exception == bytes.fromhex("00 01 00 00 00 03 FF 86 06"), exception.hex()

            started = time.monotonic()
            gateway.send_signal(signal.SIGINT)
            assert backend.recv(1) == b"", "后端收到了额外请求"
            check_shutdown(gateway, started, cancelled=1)


if __name__ == "__main__":
    run_idle_case(with_clients=False)
    run_idle_case(with_clients=True)

    with gateway_variant(3) as executable:
        run_pending_case(executable)

    # 前三轮结束后，新的网关仍能绑定同一个端口。
    run_idle_case(with_clients=False)
