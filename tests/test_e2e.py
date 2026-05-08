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

    # Create mock project internally
    project_path = os.path.join(tmp_dir, "e2e_project")
    os.makedirs(os.path.join(project_path, "src"), exist_ok=True)
    os.makedirs(os.path.join(project_path, "docs"), exist_ok=True)

    with open(os.path.join(project_path, "src", "math_utils.py"), "w") as f:
        f.write("""class Calculator:\n    \"\"\"A simple calculator class.\"\"\"\n    def add(self, a, b):\n        return a + b\n\n    def subtract(self, a, b):\n        return a - b\n\ndef power(base, exp):\n    return base ** exp\n""")

    with open(os.path.join(project_path, "README.md"), "w") as f:
        f.write("# E2E Project\\nThis is a test project for Kestr end-to-end verification.\\nIt contains some python code and documentation.\\n")

    with open(os.path.join(project_path, "docs", "notes.txt"), "w") as f:
        f.write("Important notes:\\n1. Tree-sitter should parse math_utils.py.\\n2. TextChunker should parse this file.\\n3. Keywords like 'calculator' or 'power' should return results.\\n")

    runtime_dir = os.path.join(tmp_dir, "runtime")
    config_dir = os.path.join(tmp_dir, ".config", "kestr")
    data_dir = os.path.join(tmp_dir, ".local", "share", "kestr")
    os.makedirs(runtime_dir, exist_ok=True)
    os.makedirs(config_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)

    print(f"DEBUG project_path: {project_path}")

    is_windows = os.name == "nt"
    test_env = {
        **os.environ,
        "XDG_RUNTIME_DIR": runtime_dir,
        "HOME": tmp_dir, 
    }

    if is_windows:
        # On Windows, the platform code appends /kestr to APPDATA and LOCALAPPDATA
        test_env["APPDATA"] = os.path.join(tmp_dir, ".config")
        test_env["LOCALAPPDATA"] = os.path.join(tmp_dir, ".local", "share")
        socket_path = "\\\\.\\pipe\\kestr.sock"
    else:
        socket_path = os.path.join(runtime_dir, "kestr", "kestr.sock")
    
    # Ensure config directory exists
    os.makedirs(os.path.join(config_dir), exist_ok=True)
    
    with open(os.path.join(config_dir, "config.json"), "w") as f:
        json.dump({
            "watch_paths": [project_path.replace("\\", "/")], # normalize for config
            "embedding_backend": "none",
            "memory_mode": "disk"
        }, f)

    bin_dir = os.environ.get("CMAKE_BINARY_DIR", os.path.join(cwd, "build"))
    
    def find_binary(name):
        ext = ".exe" if is_windows else ""
        binary_name = name + ext
        # Try different possible locations
        candidates = [
            os.path.join(bin_dir, "bin", binary_name),
            os.path.join(bin_dir, "bin", "Release", binary_name),
            os.path.join(bin_dir, "bin", "Debug", binary_name),
            os.path.join(bin_dir, "Release", binary_name),
            os.path.join(bin_dir, binary_name)
        ]
        for c in candidates:
            if os.path.exists(c):
                return c
        return os.path.join(bin_dir, "bin", binary_name) # Fallback

    kestrd_bin = find_binary("kestrd")
    mcp_bin = find_binary("kestr-mcp")

    print(f"DEBUG: kestrd_bin={kestrd_bin}")
    print(f"DEBUG: mcp_bin={mcp_bin}")

    print(f"Starting kestrd for E2E...")
    daemon = subprocess.Popen([kestrd_bin], env=test_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    # Wait for socket and for indexing to finish
    max_retries = 100
    socket_ready = False
    while max_retries > 0:
        if is_windows:
            # On Windows, we try to see if the pipe exists by attempting to open it
            try:
                with open(socket_path, 'r+b'):
                    socket_ready = True
                    break
            except:
                pass
        else:
            if os.path.exists(socket_path):
                socket_ready = True
                break
        time.sleep(0.1)
        max_retries -= 1
    
    if not socket_ready:
        print("kestrd failed to create socket/pipe")
        try:
            out, err = daemon.communicate(timeout=2)
            print(f"daemon STDOUT:\n{out}")
            print(f"daemon STDERR:\n{err}")
        except Exception as e:
            print(f"Could not capture daemon logs: {e}")
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
                # Parse human-readable format: "key: value"
                status = {}
                for line in status_text.splitlines():
                    if ":" in line:
                        k, v = line.split(":", 1)
                        try:
                            status[k.strip()] = json.loads(v.strip())
                        except:
                            status[k.strip()] = v.strip()
                
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
        if resp is None or "result" not in resp:
            print(f"FAILED: Query returned invalid response: {resp}")
            sys.exit(1)
            
        results = resp["result"]["content"][0]["text"]
        print(f"DEBUG Query results: {results}")
        
        try:
            assert "Calculator" in results
            # The output format is "Symbol: Calculator (class)"
            assert "(class)" in results or "class Calculator" in results
            print("Test 1 OK")
        except AssertionError as e:
            print(f"FAILED Test 1: Expected 'Calculator' and '(class)' in results.\nActual results: {results}")
            # Try to get daemon logs
            try:
                daemon.terminate()
                out, err = daemon.communicate(timeout=2)
                print(f"daemon STDOUT:\n{out}")
                print(f"daemon STDERR:\n{err}")
            except: pass
            raise e

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
        print(f"DEBUG Query results (power): {results}")

        try:
            assert "power" in results
            assert "(function)" in results
            print("Test 2 OK")
        except AssertionError as e:
            print(f"FAILED Test 2: Expected 'power' and '(function)' in results.\nActual results: {results}")
            raise e


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
        print(f"DEBUG Query results (verification): {results}")

        try:
            assert "verification" in results
            print("Test 3 OK")
        except AssertionError as e:
            print(f"FAILED Test 3: Expected 'verification' in results.\nActual results: {results}")
            raise e


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
