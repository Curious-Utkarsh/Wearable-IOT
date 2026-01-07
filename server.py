import asyncio
import websockets
import socket

HOST = "0.0.0.0"
PORT = 1234

connected_clients = set()

async def handler(websocket):
    client_ip = websocket.remote_address
    connected_clients.add(websocket)
    print(f"✓ Client connected: {client_ip}")
    print(f"  Total clients: {len(connected_clients)}")

    try:
        async for message in websocket:
            print(f"✓ Received from {client_ip}: {message}")
            
            # Echo back to sender (optional)
            # await websocket.send(f"ACK: {message}")
            
            # Broadcast to all other clients (if you want the dashboard to receive ESP32 data)
            for client in connected_clients:
                if client != websocket:
                    try:
                        await client.send(message)
                        print(f"  → Forwarded to another client")
                    except:
                        pass
                        
    except websockets.exceptions.ConnectionClosed as e:
        print(f"✗ Client disconnected: {client_ip}")
        print(f"   Reason: {e}")
    finally:
        connected_clients.remove(websocket)
        print(f"  Remaining clients: {len(connected_clients)}")

async def main():
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    
    print(f"=" * 60)
    print(f"WebSocket Server Starting")
    print(f"Listening on: ws://{HOST}:{PORT}")
    print(f"Your PC IP: {local_ip}")
    print(f"=" * 60)
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
        print("\n✓ Server stopped")
