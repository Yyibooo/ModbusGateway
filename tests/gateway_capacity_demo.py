import select
import socket
import time

from gateway_disconnect_demo import (
    GATEWAY_ENV,
    TIMEOUT,
    connect_client,
    finish_read,
    gateway_variant,
    receive_frame,
    request,
    response,
    start_gateway,
)


def run_case(executable, queue_timeout_ms):
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.settimeout(TIMEOUT)
        listener.bind(("127.0.0.1", 1502))
        listener.listen(1)

        env = {**GATEWAY_ENV, "MBGW_QUEUE_CAPACITY": "1", "MBGW_QUEUE_TIMEOUT_MS": str(queue_timeout_ms)}
        with start_gateway(executable, env=env) as gateway:
            backend, _ = listener.accept()
            with backend, connect_client() as a, connect_client() as b, connect_client() as c:
                backend.settimeout(TIMEOUT)
                a.sendall(request(0))
                first = receive_frame(backend)
                assert first[7:] == request(0)[7:], first.hex()
                print("后端扣住 A 的回复，B 和 C 争用唯一等待槽位")

                write = bytes.fromhex("00 01 00 00 00 06 FF 06 00 01 03 E7")
                b.sendall(write if queue_timeout_ms else request(1))
                c.sendall(write if queue_timeout_ms else request(2))
                exception_function = 0x86 if queue_timeout_ms else 0x83
                expected_busy = bytes.fromhex(f"00 01 00 00 00 03 FF {exception_function:02x} 06")
                readable, _, _ = select.select([b, c], [], [], TIMEOUT)
                assert len(readable) == 1, "A 尚未完成时应有且仅有一个客户端立即收到回复"
                rejected = readable[0]
                exception = receive_frame(rejected)
                assert exception == expected_busy, exception.hex()
                accepted, address, value = (c, 2, 789) if rejected is b else (b, 1, 456)
                accepted_name, rejected_name = ("C", "B") if rejected is b else ("B", "C")
                print(f"{rejected_name} 因队满收到 0x06，{accepted_name} 已入队")

                if queue_timeout_ms:
                    # 收到队满回复时，唯一的槽位已经被占用。
                    time.sleep((queue_timeout_ms + 50) / 1000)

                backend.sendall(response(first, 123))
                assert receive_frame(a) == response(request(0), 123)
                if queue_timeout_ms:
                    exception = receive_frame(accepted)
                    assert exception == expected_busy, exception.hex()
                    with connect_client() as d:
                        d.sendall(request(2))
                        # 后端收到的下一帧必须是 D 的读请求。
                        finish_read(backend, d, 2, 789)
                    print(f"通过：A 读到 123，{accepted_name} 的写请求过期，D 读到 789")
                else:
                    finish_read(backend, accepted, address, value)
                    print(f"通过：A 读到 123，{accepted_name} 读到 {value}")

            assert gateway.wait(timeout=TIMEOUT) == 0
            logs = gateway.stdout.read().decode()
            print(logs, end="")
            assert "队列已满" in logs, logs
            assert ("排队过期" in logs) == bool(queue_timeout_ms), logs
            assert f"完成=2 失败=0 其中超时=0 排队拒绝={int(bool(queue_timeout_ms))}" in logs, logs


if __name__ == "__main__":
    for queue_timeout_ms, clients in ((0, 3), (100, 4)):
        with gateway_variant(clients) as executable:
            run_case(executable, queue_timeout_ms)
