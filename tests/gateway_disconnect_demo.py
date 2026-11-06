from contextlib import contextmanager
from pathlib import Path
import re
import select
import socket
import struct
import subprocess
import tempfile
import time

PROJECT = Path(__file__).resolve().parents[1]
GATEWAY = PROJECT / "build" / "gateway_shared_backend_demo"
TIMEOUT = 3


def receive_exact(peer, size):
    data = bytearray()
    while len(data) < size:
        chunk = peer.recv(size - len(data))
        if not chunk:
            raise RuntimeError("连接在报文收齐前关闭")
        data.extend(chunk)
    return bytes(data)


def receive_frame(peer):
    header = receive_exact(peer, 6)
    size = int.from_bytes(header[4:6], "big")
    return header + receive_exact(peer, size)


def request(address):
    return bytes.fromhex("00 01 00 00 00 06 FF 03") + struct.pack(">HH", address, 1)


def response(received, value):
    return received[:4] + bytes.fromhex("00 05 FF 03 02") + struct.pack(">H", value)


def reset_connection(peer):
    peer.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    peer.close()


@contextmanager
def start_gateway(executable):
    with subprocess.Popen(
        ["stdbuf", "-oL", str(executable)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    ) as gateway:
        try:
            wait_for_log(gateway, "共享后端实验监听 1506")
            yield gateway
        finally:
            if gateway.poll() is None:
                gateway.terminate()
                gateway.wait(timeout=TIMEOUT)


def wait_for_log(gateway, marker):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        remaining = max(0, deadline - time.monotonic())
        readable, _, _ = select.select([gateway.stdout], [], [], remaining)
        if not readable:
            break
        line = gateway.stdout.readline().decode().strip()
        if not line:
            break
        print(line)
        if marker in line:
            return line
    raise RuntimeError(f"网关日志中未看到 {marker!r}")


def closed_fd(gateway):
    line = wait_for_log(gateway, "：已关闭")
    match = re.fullmatch(r"连接 fd=(\d+)：已关闭", line)
    if match is None:
        raise RuntimeError(f"无法识别关闭日志：{line}")
    return int(match.group(1))


def finish_read(backend, client, address, value):
    received = receive_frame(backend)
    assert received[7:] == request(address)[7:], received.hex()
    backend.sendall(response(received, value))
    reply = receive_frame(client)
    assert reply == response(request(address), value), reply.hex()


def run_case(executable, reuse_fd):
    label = "复用描述符" if reuse_fd else "另一客户端已连接"
    print(f"\n实验：{label}")

    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.settimeout(TIMEOUT)
        listener.bind(("127.0.0.1", 1502))
        listener.listen(1)

        with start_gateway(executable) as gateway:
            backend, _ = listener.accept()
            with backend, connect_client() as a:
                backend.settimeout(TIMEOUT)
                a.sendall(request(0))
                first = receive_frame(backend)
                assert first[7:] == request(0)[7:], first.hex()
                print("后端已收到 A 的请求，暂时扣住回复")

                if reuse_fd:
                    reset_connection(a)
                    backend.sendall(response(first, 123))
                    fd_a = closed_fd(gateway)

                    # accept() 可能在有新连接前就预留一个描述符。
                    # 同时接入 B 和 C，让其中一个实际复用 A 的编号。
                    with connect_client() as b, connect_client() as c:
                        b.sendall(request(2))
                        finish_read(backend, b, 2, 789)
                        fd_b = closed_fd(gateway)
                        c.sendall(request(1))
                        finish_read(backend, c, 1, 456)
                        fd_c = closed_fd(gateway)
                    assert fd_a in (fd_b, fd_c), (fd_a, fd_b, fd_c)
                    print(f"通过：A fd={fd_a}，B fd={fd_b} 读到 789，C fd={fd_c} 读到 456")
                else:
                    with connect_client() as b:
                        b.sendall(request(2))
                        reset_connection(a)
                        backend.sendall(response(first, 123))
                        fd_a = closed_fd(gateway)
                        finish_read(backend, b, 2, 789)
                    fd_b = closed_fd(gateway)
                    assert fd_a != fd_b, (fd_a, fd_b)
                    print(f"通过：A fd={fd_a} 已断开，B fd={fd_b} 读到 789")

            assert gateway.wait(timeout=TIMEOUT) == 0


def connect_client():
    return socket.create_connection(("127.0.0.1", 1506), timeout=TIMEOUT)


if __name__ == "__main__":
    run_case(GATEWAY, reuse_fd=False)

    with tempfile.TemporaryDirectory(prefix="gateway-fd-reuse-") as directory:
        subprocess.run(
            [
                "make",
                f"BUILD_DIR={directory}",
                "CFLAGS=-Wall -Wextra -g -DGATEWAY_DEMO_CLIENT_COUNT=3",
                "gateway_shared_backend_demo",
            ],
            cwd=PROJECT,
            check=True,
        )
        run_case(Path(directory) / GATEWAY.name, reuse_fd=True)
