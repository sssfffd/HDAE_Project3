import socket, struct, sys, time

PC_IP = "192.168.2.10" # 노트북 유선 NIC
DST_IP = "192.168.2.20"
PORT = 30509

payload = b"250;250;0;0;0;0;0;1;1\n"

header = struct.pack(">HHIIBBBB", 0x0100, 0x0103, 8 + len(payload),
0x00000001, 0x01, 0x01, 0x00, 0x00)
pkt = header + payload

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", PORT))

# Non-blocking receive to avoid stalling send cadence
sock.setblocking(False)

INTERVAL_SEC = 0.010  # 10 ms
print(f"sending SOME/IP request every {int(INTERVAL_SEC*1000)} ms to {DST_IP}:{PORT} (Ctrl+C to stop)")
next_send = time.perf_counter()
sent = 0
recv_count = 0
try:
    while True:
        now = time.perf_counter()
        if now >= next_send:
            sock.sendto(pkt, (DST_IP, PORT))
            sent += 1
            # schedule next send; if delayed, realign
            next_send += INTERVAL_SEC
            if now - next_send > INTERVAL_SEC:
                next_send = now + INTERVAL_SEC

        # Non-blocking read of any responses
        try:
            data, addr = sock.recvfrom(2048)
            recv_count += 1
            if recv_count % 100 == 1:
                print(f"rx[{recv_count}] {len(data)}B from {addr}, msgType={hex(data[14])}, payload={data[16:]}")
        except (BlockingIOError, InterruptedError):
            pass

        time.sleep(0)
except KeyboardInterrupt:
    print(f"\nstopped. sent={sent}, received={recv_count}")
finally:
    sock.close()

