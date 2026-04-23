import asyncio
import websockets
import socket
import sys
import io
import json
import re

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

HOST = "0.0.0.0"
PORT = 1234

connected_clients = set()
imu_buffer = ""

def parse_imu_buffer(buf):
    result = {}
    matches = re.findall(r'(AX|AY|AZ|GX|GY|GZ):(-?\d+\.?\d*)', buf)
    for key, val in matches:
        try:
            result[key] = float(val)
        except ValueError:
            print(f"    [!] Skipping bad value: {key}={val}")
    return result

async def handler(websocket):
    global imu_buffer
    client_ip = websocket.remote_address
    connected_clients.add(websocket)
    print(f"[+] Client connected: {client_ip}")
    print(f"    Total clients: {len(connected_clients)}")

    try:
        async for message in websocket:
            print(f"[<] Received: {message}")

            try:
                data = json.loads(message)
            except json.JSONDecodeError:
                print("    [!] Invalid JSON, skipping")
                continue

            if data.get("dev") == "IMU" and "raw" in data:
                imu_buffer += data["raw"]

                required = ["AX:", "AY:", "AZ:", "GX:", "GY:", "GZ:"]
                if all(k in imu_buffer for k in required):
                    parsed = parse_imu_buffer(imu_buffer)
                    imu_buffer = ""  # reset regardless of success

                    if len(parsed) == 6:
                        complete_msg = json.dumps({
                            "dev": "IMU",
                            "ax": parsed["AX"],
                            "ay": parsed["AY"],
                            "az": parsed["AZ"],
                            "gx": parsed["GX"],
                            "gy": parsed["GY"],
                            "gz": parsed["GZ"]
                        })
                        print(f"    [✓] IMU assembled: {complete_msg}")

                        for client in connected_clients.copy():
                            if client != websocket:
                                try:
                                    await client.send(complete_msg)
                                    print(f"    [>] Forwarded assembled IMU")
                                except:
                                    pass
                    else:
                        print(f"    [!] IMU parse incomplete, got {len(parsed)}/6 fields, discarding")
                else:
                    print(f"    [~] IMU fragment buffered, waiting for more...")
                continue

            # All other sensors forward as-is
            for client in connected_clients.copy():
                if client != websocket:
                    try:
                        await client.send(message)
                        print(f"    [>] Forwarded to another client")
                    except:
                        pass

    except websockets.exceptions.ConnectionClosed as e:
        print(f"[-] Client disconnected: {client_ip} | Reason: {e}")
    finally:
        connected_clients.discard(websocket)
        imu_buffer = ""  # clear buffer on disconnect
        print(f"    Remaining clients: {len(connected_clients)}")

async def main():
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)

    print("=" * 60)
    print("WebSocket Server Starting")
    print(f"Listening on: ws://{HOST}:{PORT}")
    print(f"Your PC IP: {local_ip}")
    print("=" * 60)
    print("Waiting for connections...\n")

    async with websockets.serve(
        handler,
        HOST,
        PORT,
        ping_interval=20,
        ping_timeout=10
    ):
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[!] Server stopped")
