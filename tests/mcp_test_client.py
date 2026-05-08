import socket
import json
import os

SOCKET_PATH = "/run/user/1000/kestr/kestr.sock"

def call_kestr(method, params=None):
    if params is None:
        params = []
    
    request = {
        "method": method,
        "params": params
    }
    
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.connect(SOCKET_PATH)
            s.sendall(json.dumps(request).encode())
            response = b""
            while True:
                data = s.recv(4096)
                if not data:
                    break
                response += data
            return json.loads(response.decode())
    except Exception as e:
        return {"error": str(e)}

def main():
    print("--- Testing Kestr MCP Server ---")
    
    # 1. Ping
    print("\n[1] Testing Ping...")
    print(call_kestr("ping"))
    
    # 2. Status
    print("\n[2] Testing Status...")
    print(json.dumps(call_kestr("status"), indent=2))
    
    # 3. Summarize Project
    print("\n[3] Testing Summarize Project...")
    # Use the project root
    print(json.dumps(call_kestr("summarize_project", ["/home/pomilon/Projects/Kestr/src"]), indent=2))
    
    # 4. List Symbols (for a known file)
    print("\n[4] Testing List Symbols...")
    file_path = "/home/pomilon/Projects/Kestr/src/main.cpp"
    print(json.dumps(call_kestr("list_symbols", [file_path]), indent=2))
    
    # 5. Find References (for a symbol found in list_symbols)
    print("\n[5] Testing Find References...")
    # Let's pick a symbol that's likely to be there, e.g., 'start_web_server'
    print(json.dumps(call_kestr("find_references", ["start_web_server"]), indent=2))
    
    # 6. Get Definition
    print("\n[6] Testing Get Definition...")
    print(json.dumps(call_kestr("get_definition", ["start_web_server"]), indent=2))
    
    # 7. Hybrid Query
    print("\n[7] Testing Hybrid Query...")
    print(json.dumps(call_kestr("query", ["web server", 3]), indent=2))

if __name__ == '__main__':
    main()
