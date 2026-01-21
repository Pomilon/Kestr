# Kestr

**Kestr** is a local-first, high-performance background daemon that turns your codebase into a semantic knowledge base. It watches your files in real-time, generates embeddings locally (or via API), and provides an instant semantic search interface for AI agents and tools.

It implements the **Model Context Protocol (MCP)**, making it plug-and-play compatible with tools like **Claude Desktop**, **Gemini CLI**, [**Plexir**](https://github.com/Pomilon/Plexir), and AI-enhanced IDEs.

## Features

*   **Sentry (File Watcher):** Real-time, recursive file monitoring using `inotify` (Linux). Automatically detects changes and queues files for re-indexing.
*   **Talon (Embeddings):** Flexible embedding engine supporting:
    *   **Local ONNX:** Runs `all-MiniLM-L6-v2` locally using ONNX Runtime (No GPU required).
    *   **Ollama:** Connects to a local Ollama instance (default fallback).
    *   **OpenAI:** Uses OpenAI's `text-embedding-3-small` (requires API key).
*   **The Librarian (Search):** Hybrid search engine combining:
    *   **Vector Search:** In-memory HNSW index for semantic understanding.
    *   **Keyword Search:** SQLite-backed fallback for exact matches or low-memory environments.
*   **The Cache:** Persistent SQLite storage for incremental updates (skips unchanged files).
*   **MCP Server:** Native integration with the Model Context Protocol.

## Installation

### Quick Install (Linux)
Use the included script to build and install Kestr as a user service:

```bash
./install.sh
```
This script will:
1.  Check dependencies.
2.  Build the project using CMake.
3.  Install binaries to `~/.local/bin/`.
4.  Set up `~/.config/kestr/config.json`.
5.  (Optional) Install and enable the `systemd` user service.

### Manual Build
**Prerequisites:** Linux (x64), C++20 (GCC 11+/Clang 14+), CMake 3.20+, SQLite3, CURL.

```bash
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

## Configuration

Kestr looks for a configuration file at `~/.config/kestr/config.json`.

**Example `config.json`:**
```json
{
    "memory_mode": "hybrid",
    "hybrid_limit": 5000,
    "embedding_backend": "ollama",
    "embedding_model": "nomic-embed-text",
    "watch_paths": [
        "/home/user/projects/kestr",
        "/home/user/docs"
    ]
}
```

### Options
| Option | Values | Description |
|--------|--------|-------------|
| `memory_mode` | `"ram"` | Loads **all** vectors into RAM for fastest search (Default). |
| | `"hybrid"` | Loads only the `hybrid_limit` most recent vectors into RAM. |
| | `"disk"` | Zero-RAM vector mode. Search relies purely on Database Keywords. |
| `hybrid_limit` | `int` | Number of chunks to keep in RAM if mode is `hybrid`. |
| `embedding_backend` | `"onnx"` | Uses local `model.onnx` files in the run directory. |
| | `"ollama"` | Uses local Ollama API (Default). |
| | `"openai"` | Uses OpenAI API (Set `OPENAI_API_KEY` env var or config). |
| `watch_paths` | `[string]`| List of absolute paths to monitor and index. |

### Local ONNX Setup
To run completely offline without Ollama:
1.  Set `"embedding_backend": "onnx"` in your `config.json`.
2.  Download the **all-MiniLM-L6-v2** ONNX model:
    *   [model.onnx](https://huggingface.co/Xenova/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx)
    *   [vocab.txt](https://huggingface.co/Xenova/all-MiniLM-L6-v2/resolve/main/vocab.txt)
3.  Place these files in one of the following locations:
    *   `~/.local/share/kestr/` (Recommended for service usage)
    *   The current working directory where you start `kestrd`.

## Usage

### 1. Start the Daemon
If installed as a service:
```bash
systemctl --user start kestr
```
Or manually:
```bash
kestrd
```

### 2. CLI Commands
Interact with the daemon using the `kestr` client:

```bash
# Check status (Queue size, Memory usage, Watch paths)
kestr status

# Add a directory to watch
kestr watch /path/to/project

# Semantic Search (Default limit: 5)
kestr query "How does the file watcher work?"

# Semantic Search with custom limit
kestr query "platform abstraction" 10

# Force Re-indexing (Scans all watch paths)
kestr reindex

# Stop the daemon
kestr shutdown
```

## MCP Integration

To use Kestr with MCP clients (like Claude Desktop), configure the tool to run `kestr-mcp`.

**Claude Desktop Config (`claude_desktop_config.json`):**
```json
{
  "mcpServers": {
    "kestr": {
      "command": "/home/YOUR_USER/.local/bin/kestr-mcp",
      "args": []
    }
  }
}
```
*Note: Ensure `kestrd` is running for `kestr-mcp` to work.*

### Available MCP Tools & Resources
*   **Tool:** `kestr_query` - Search the codebase. Params: `query` (string), `limit` (int, optional).
*   **Resource:** `kestr://<path>` - Read any indexed file content.
*   **Resource List:** Browse all indexed files.

## License
MIT
