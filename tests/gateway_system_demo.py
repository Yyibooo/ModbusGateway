from contextlib import contextmanager
from pathlib import Path
import os
import select
import shutil
import socket
import struct
import subprocess
import tempfile
import termios
import time

from gateway_disconnect_demo import (
    GATEWAY_ENV,
    GATEWAY, PROJECT, TIMEOUT, receive_frame, request, response, start_gateway, wait_for_log,
)

RTU_SERVER = Path(os.environ.get("RTU_SERVER_BINARY", PROJECT / "build" / "modbus_rtu_server"))
SOCAT = shutil.which("socat") or Path.home() / ".local" / "bin" / "socat"


@contextmanager
def logged_process(*args):
    with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0) as process:
        try:
            yield process
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=TIMEOUT)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=TIMEOUT)


@contextmanager
def serial_pair():
    with tempfile.TemporaryDirectory(prefix="gateway-rtu-") as directory:
        server, client = Path(directory) / "server", Path(directory) / "client"
        with logged_process(
            SOCAT, "-d", "-d", f"pty,raw,echo=0,link={server}", f"pty,raw,echo=0,link={client}"
        ) as link:
            wait_for_log(link, "starting data transfer loop")
            yield server, client


def tcp_request(transaction, unit, function, address, value):
    return struct.pack(">HHHBBHH", transaction, 0, 6, unit, function, address, value)


def expected_exception(transaction, unit, function, code):
    return struct.pack(">HHHBBB", transaction, 0, 3, unit, function | 0x80, code)


def expected_values(transaction, values):
    data = struct.pack(f">{len(values)}H", *values)
    return struct.pack(">HHHBBB", transaction, 0, 3 + len(data), 1, 3, len(data)) + data


def available_port(address):
    with socket.socket() as probe:
        probe.bind((address, 0))
        return probe.getsockname()[1]


def check_one_request(peer, extra_request):
    try:
        peer.sendall(extra_request)
        assert peer.recv(1) == b"", "同一连接处理了第二条请求"
    except (BrokenPipeError, ConnectionResetError):
        pass


def rtu_case(mode):
    listen_address = "127.0.0.2"
    port = available_port(listen_address)
    env = {**GATEWAY_ENV, "MBGW_LISTEN_ADDRESS": listen_address, "MBGW_LISTEN_PORT": str(port),
           "MBGW_QUEUE_CAPACITY": "3", "MBGW_RESPONSE_TIMEOUT_MS": "200"}
    slave_args = {"normal": ("2",), "invalid": ("2",), "missing": ("1",),
                  "silent": ("2", "--drop-first")}[mode]
    arguments = ()
    if mode == "silent":
        # 有效的位置参数覆盖同名用途的环境变量。
        env["MBGW_RESPONSE_TIMEOUT_MS"] = "200ms"
        arguments = ("200",)
    with serial_pair() as (server, client), logged_process(RTU_SERVER, server, *slave_args) as slave:
        wait_for_log(slave, "等待请求")
        with start_gateway(GATEWAY, client, *arguments, env=env, port=port) as gateway:
            with socket.create_connection((listen_address, port), TIMEOUT) as a, \
                 socket.create_connection((listen_address, port), TIMEOUT) as b:
                if mode == "normal":
                    write = tcp_request(1, 1, 6, 1, 999)
                    a.sendall(write[:4])
                    time.sleep(0.02)
                    a.sendall(write[4:])
                    assert receive_frame(a) == write
                    check_one_request(a, tcp_request(3, 1, 6, 2, 222))
                    expected = (123, 999, 789)
                else:
                    unit, function, address, value, code = {
                        "invalid": (1, 3, 2, 2, 0x02),
                        "missing": (2, 3, 0, 3, 0x0B),
                        "silent": (1, 3, 0, 3, 0x0B),
                    }[mode]
                    a.sendall(tcp_request(1, unit, function, address, value))
                    assert receive_frame(a) == expected_exception(1, unit, function, code)
                    expected = (123, 456, 789)
                b.sendall(tcp_request(2, 1, 3, 0, 3))
                assert receive_frame(b) == expected_values(2, expected)
            assert gateway.wait(timeout=TIMEOUT) == slave.wait(timeout=TIMEOUT) == 0
            gateway_log = gateway.stdout.read().decode()
            slave_log = slave.stdout.read().decode()
            assert ("跳过回复" in slave_log) == (mode == "silent"), slave_log
            if mode == "normal":
                assert "完成=2" in gateway_log, gateway_log
            print(f"RTU {mode}: B={expected}")


def with_rtu_crc(payload):
    crc = 0xFFFF
    for byte in payload:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return payload + crc.to_bytes(2, "little")


def serve_rtu_read(serial, values, delay=0):
    deadline = time.monotonic() + TIMEOUT
    frame = bytearray()
    while len(frame) < 8:
        remaining = max(0, deadline - time.monotonic())
        readable, _, _ = select.select([serial], [], [], remaining)
        assert readable, "RTU 请求未收齐"
        chunk = serial.read(8 - len(frame))
        assert chunk, "虚拟串口提前关闭"
        frame.extend(chunk)

    unit = frame[0]
    assert frame == with_rtu_crc(bytes([unit, 3, 0, 0, 0, 1])), frame.hex()
    if delay:
        time.sleep(delay)
    reply = with_rtu_crc(struct.pack(">BBBH", unit, 3, 2, values[unit]))
    assert serial.write(reply) == len(reply)


def check_two_units():
    port = available_port("127.0.0.1")
    env = {**GATEWAY_ENV, "MBGW_LISTEN_PORT": str(port), "MBGW_RTU_BAUD": "19200",
           "MBGW_RTU_STOP_BITS": "2"}
    with serial_pair() as (server, client), server.open("r+b", buffering=0) as serial:
        with start_gateway(GATEWAY, client, env=env, port=port) as gateway:
            with client.open("rb", buffering=0) as tty:
                assert termios.tcgetattr(tty)[2] & termios.CSTOPB, "RTU 停止位未设置为 2"
            for unit, value in ((1, 123), (2, 456)):
                with socket.create_connection(("127.0.0.1", port), TIMEOUT) as peer:
                    peer.sendall(tcp_request(unit, unit, 3, 0, 1))
                    serve_rtu_read(serial, {1: 123, 2: 456})
                    expected = struct.pack(">HHHBBBH", unit, 0, 5, unit, 3, 2, value)
                    assert receive_frame(peer) == expected
            assert gateway.wait(timeout=TIMEOUT) == 0
            logs = gateway.stdout.read().decode()
            assert "19200 8N2" in logs, logs
            print("两个模拟 RTU 设备：设备 1 读到 123，设备 2 读到 456")


def tcp_backend_case(disconnect):
    with socket.socket() as listener:
        listener.bind(("127.0.0.2", 0))
        listener.settimeout(TIMEOUT)
        listener.listen(1)
        port = available_port("127.0.0.1")
        env = {**GATEWAY_ENV, "MBGW_BACKEND_ADDRESS": "127.0.0.2",
               "MBGW_BACKEND_PORT": str(listener.getsockname()[1]), "MBGW_LISTEN_PORT": str(port),
               "MBGW_RESPONSE_TIMEOUT_MS": "200"}
        with start_gateway(GATEWAY, env=env, port=port) as gateway:
            backend, _ = listener.accept()
            process = Path(f"/proc/{gateway.pid}")
            fd_baseline = len(list((process / "fd").iterdir()))
            thread_baseline = len(list((process / "task").iterdir()))
            with backend:
                backend.settimeout(TIMEOUT)
                for index in range(2):
                    with socket.create_connection(("127.0.0.1", port), TIMEOUT) as peer:
                        peer.sendall(request(index))
                        if disconnect:
                            if index == 0:
                                assert receive_frame(backend)[7:] == request(index)[7:]
                                backend.close()
                            assert receive_frame(peer) == expected_exception(1, 255, 3, 0x0A)
                        else:
                            received = receive_frame(backend)
                            assert received[7:] == request(index)[7:]
                            backend.sendall(response(received, 123 + index))
                            assert receive_frame(peer) == response(request(index), 123 + index)

                    if index == 0 and not disconnect:
                        wait_for_log(gateway, "已关闭")
                        assert len(list((process / "fd").iterdir())) == fd_baseline
                        deadline = time.monotonic() + TIMEOUT
                        while len(list((process / "task").iterdir())) != thread_baseline:
                            assert time.monotonic() < deadline, "连接线程未退出"
                            time.sleep(0.01)
                        print(f"连接结束后，FD 回到 {fd_baseline}，线程数回到 {thread_baseline}")
            assert gateway.wait(timeout=TIMEOUT) == 0
            print("TCP 后端断开：两条请求收到 0x0A" if disconnect else "TCP 后端重启：两条请求正常")


def check_invalid_config():
    for name, value in (
        ("MBGW_LISTEN_ADDRESS", "localhost"), ("MBGW_LISTEN_PORT", "65536"),
        ("MBGW_RTU_PARITY", "X"), ("MBGW_RTU_BAUD", "0"), ("MBGW_RTU_DATA_BITS", "9"),
        ("MBGW_RTU_STOP_BITS", "0"), ("MBGW_QUEUE_CAPACITY", "0"),
        ("MBGW_QUEUE_TIMEOUT_MS", "-1"), ("MBGW_SEND_TIMEOUT_MS", "0"),
        ("MBGW_RESPONSE_TIMEOUT_MS", "200ms"),
    ):
        result = subprocess.run([GATEWAY], env={**GATEWAY_ENV, name: value}, capture_output=True,
                                text=True, timeout=TIMEOUT)
        assert result.returncode == 1 and name in result.stderr, result
    print("10 个非法配置全部在启动时被拒绝")


if __name__ == "__main__":
    check_invalid_config()
    for mode in ("normal", "invalid", "missing", "silent"):
        rtu_case(mode)
    check_two_units()
    tcp_backend_case(disconnect=True)
    tcp_backend_case(disconnect=False)
