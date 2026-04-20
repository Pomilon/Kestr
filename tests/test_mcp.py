import subprocess
import json
import os
import time
import socket

def test_mcp():
    # Setup temporary directories for testing
    cwd = os.getcwd()
    tmp_dir = os.path.join(cwd, "tmp_test")
    if os.path.exists(tmp_dir):
        import shutil
        shutil.rmtree(tmp_dir)
    os.makedirs(tmp_dir)

    runtime_dir = os.path.join(tmp_dir, "runtime")
    config_dir = os.path.join(tmp_dir, ".config", "kestr")
    data_dir = os.path.join(tmp_dir, ".local", "share", "kestr")
    os.makedirs(runtime_dir, exist_ok=True)
    os.makedirs(config_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)

    test_env = {
        **os.environ,
        "XDG_RUNTIME_DIR": runtime_dir,
        "HOME": tmp_dir, 
    }

    # The socket will be at runtime_dir/kestr/kestr.sock according to platform_linux.cpp
    socket_path = os.path.join(runtime_dir, "kestr", "kestr.sock")
    
    # We might need a dummy config to avoid downloads or errors
    with open(os.path.join(config_dir, "config.json"), "w") as f:
        json.dump({
            "watch_paths": [],
            "embedding_backend": "none",
            "memory_mode": "disk"
        }, f)

    # Use environment variable if provided, otherwise try relative paths
    bin_dir = os.environ.get("CMAKE_BINARY_DIR", os.path.join(cwd, "build"))
    kestrd_bin = os.path.join(bin_dir, "bin", "kestrd")
    mcp_bin = os.path.join(bin_dir, "bin", "kestr-mcp")

    print(f"DEBUG: bin_dir={bin_dir}")
    print(f"DEBUG: kestrd_bin={kestrd_bin}")
    print(f"DEBUG: mcp_bin={mcp_bin}")

    if not os.path.exists(kestrd_bin):
        # Fallback for manual run from root
        kestrd_bin = os.path.join(cwd, "build", "bin", "kestrd")
        mcp_bin = os.path.join(cwd, "build", "bin", "kestr-mcp")
        print(f"DEBUG: Fallback kestrd_bin={kestrd_bin}")

    if not os.path.exists(kestrd_bin):
        print(f"Error: kestrd binary not found at {kestrd_bin}")
        return False

    print(f"Starting kestrd (socket expected at {socket_path})...")
    daemon = subprocess.Popen([kestrd_bin], env=test_env)
    
    # Wait for socket to appear
    max_retries = 50
    while not os.path.exists(socket_path) and max_retries > 0:
        time.sleep(0.1)
        max_retries -= 1
    
    if not os.path.exists(socket_path):
        print(f"kestrd failed to start or create socket at {socket_path}")
        daemon.terminate()
        return False

    print(f"Starting kestr-mcp from {mcp_bin}...")
    mcp = subprocess.Popen([mcp_bin], 
                           stdin=subprocess.PIPE, 
                           stdout=subprocess.PIPE, 
                           stderr=subprocess.PIPE,
                           env=test_env,
                           text=True)

    def send_mcp(req):
        mcp.stdin.write(json.dumps(req) + "\n")
        mcp.stdin.flush()
        line = mcp.stdout.readline()
        return json.loads(line)

    try:
        # 1. Initialize
        print("Sending initialize...")
        resp = send_mcp({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        assert resp["id"] == 1
        assert "capabilities" in resp["result"]
        print("Initialize OK")

        # 2. Tools List
        print("Sending tools/list...")
        resp = send_mcp({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
        assert resp["id"] == 2
        tools = resp["result"]["tools"]
        assert any(t["name"] == "kestr_query" for t in tools)
        print("Tools List OK")

        # 3. Resources List
        print("Sending resources/list...")
        resp = send_mcp({"jsonrpc": "2.0", "id": 3, "method": "resources/list", "params": {}})
        assert resp["id"] == 3
        assert "resources" in resp["result"]
        print("Resources List OK")

        # 4. Tool Call (kestr_query)
        print("Sending tools/call (kestr_query)...")
        resp = send_mcp({
            "jsonrpc": "2.0", 
            "id": 4, 
            "method": "tools/call", 
            "params": {
                "name": "kestr_query",
                "arguments": {"query": "test"}
            }
        })
        assert resp["id"] == 4
        assert "content" in resp["result"]
        print("Tool Call OK")

        print("All MCP tests passed!")
        return True

    except Exception as e:
        print(f"Test failed: {e}")
        # Print stderr from mcp for debugging
        print("MCP Stderr:", mcp.stderr.read())
        return False
    finally:
        mcp.terminate()
        daemon.terminate()
        import shutil
        shutil.rmtree(tmp_dir)

if __name__ == "__main__":
    import sys
    if test_mcp():
        sys.exit(0)
    else:
        sys.exit(1)
