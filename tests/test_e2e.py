import subprocess
import json
import os
import time
import shutil

def test_e2e():
    cwd = os.getcwd()
    tmp_dir = os.path.join(cwd, "tmp_e2e")
    if os.path.exists(tmp_dir):
        shutil.rmtree(tmp_dir)
    os.makedirs(tmp_dir)

    runtime_dir = os.path.join(tmp_dir, "runtime")
    config_dir = os.path.join(tmp_dir, ".config", "kestr")
    data_dir = os.path.join(tmp_dir, ".local", "share", "kestr")
    os.makedirs(runtime_dir, exist_ok=True)
    os.makedirs(config_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)

    project_path = os.path.join(cwd, "e2e_project")
    # Make sure project_path is absolute and exists
    project_path = os.path.abspath(project_path)
    if not os.path.exists(project_path):
        # Maybe we are in build dir
        project_path = os.path.abspath(os.path.join(cwd, "..", "e2e_project"))
    
    print(f"DEBUG project_path: {project_path}")

    test_env = {
        **os.environ,
        "XDG_RUNTIME_DIR": runtime_dir,
        "HOME": tmp_dir, 
    }

    socket_path = os.path.join(runtime_dir, "kestr", "kestr.sock")
    
    with open(os.path.join(config_dir, "config.json"), "w") as f:
        json.dump({
            "watch_paths": [project_path],
            "embedding_backend": "none",
            "memory_mode": "disk"
        }, f)

    bin_dir = os.environ.get("CMAKE_BINARY_DIR", os.path.join(cwd, "build"))
    kestrd_bin = os.path.join(bin_dir, "bin", "kestrd")
    mcp_bin = os.path.join(bin_dir, "bin", "kestr-mcp")

    print(f"Starting kestrd for E2E...")
    daemon = subprocess.Popen([kestrd_bin], env=test_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    # Wait for socket and for indexing to finish
    max_retries = 100
    socket_ready = False
    while max_retries > 0:
        if os.path.exists(socket_path):
            socket_ready = True
            break
        time.sleep(0.1)
        max_retries -= 1
    
    if not socket_ready:
        print("kestrd failed to create socket")
        daemon.terminate()
        return False

    print("Starting kestr-mcp for E2E queries...")
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
        if not line:
            return None
        return json.loads(line)

    try:
        send_mcp({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        
        # Wait for queue_size to be 0
        print("Waiting for indexing to complete...")
        indexed = False
        for _ in range(30): # 30 seconds max
            resp = send_mcp({
                "jsonrpc": "2.0", "id": 100, "method": "tools/call", 
                "params": {
                    "name": "kestr_status",
                    "arguments": {}
                }
            })
            if resp and "result" in resp:
                status_text = resp["result"]["content"][0]["text"]
                status = json.loads(status_text)
                print(f"DEBUG Status: {status}")
                if status.get("queue_size", 0) == 0 and status.get("total_files", 0) > 0:
                    indexed = True
                    break
            time.sleep(1)
        
        if not indexed:
            print("Indexing timed out or no files found.")
            # We continue anyway to see results, or fail early
        
        # Test 1: Find the Class (Structural)
        print("Querying for 'Calculator' (structural)...")
        resp = send_mcp({
            "jsonrpc": "2.0", "id": 2, "method": "tools/call", 
            "params": {
                "name": "kestr_query",
                "arguments": {"query": "Calculator"}
            }
        })
        results = resp["result"]["content"][0]["text"]
        assert "Calculator" in results
        assert "symbol_type: class" in results or "class Calculator" in results
        print("Test 1 OK")

        # Test 2: Find the function with type_filter
        print("Querying for 'power' with type_filter='function'...")
        resp = send_mcp({
            "jsonrpc": "2.0", "id": 3, "method": "tools/call", 
            "params": {
                "name": "kestr_query",
                "arguments": {"query": "power", "type_filter": "function"}
            }
        })
        results = resp["result"]["content"][0]["text"]
        assert "def power" in results
        print("Test 2 OK")

        # Test 3: Find markdown content (fallback chunker)
        print("Querying for 'verification' (README.md fallback)...")
        resp = send_mcp({
            "jsonrpc": "2.0", "id": 4, "method": "tools/call", 
            "params": {
                "name": "kestr_query",
                "arguments": {"query": "verification"}
            }
        })
        results = resp["result"]["content"][0]["text"]
        assert "end-to-end verification" in results
        print("Test 3 OK")

        # Test 4: Filter by language
        print("Querying for 'add' with language='python'...")
        resp = send_mcp({
            "jsonrpc": "2.0", "id": 5, "method": "tools/call", 
            "params": {
                "name": "kestr_query",
                "arguments": {"query": "add", "language": "python"}
            }
        })
        results = resp["result"]["content"][0]["text"]
        assert "def add" in results
        print("Test 4 OK")

        # Test 5: Negative filter (language=cpp shouldn't return python code)
        print("Querying for 'add' with language='cpp' (should be empty)...")
        resp = send_mcp({
            "jsonrpc": "2.0", "id": 6, "method": "tools/call", 
            "params": {
                "name": "kestr_query",
                "arguments": {"query": "add", "language": "cpp"}
            }
        })
        # If no results, it might return a message or empty list depending on logic
        results = resp["result"]["content"][0]["text"]
        assert "def add" not in results
        print("Test 5 OK")

        print("All E2E tests passed successfully!")
        return True

    except Exception as e:
        print(f"E2E Test failed: {e}")
        return False
    finally:
        mcp.terminate()
        daemon.terminate()
        shutil.rmtree(tmp_dir)

if __name__ == "__main__":
    import sys
    if test_e2e():
        sys.exit(0)
    else:
        sys.exit(1)
