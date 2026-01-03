# Kestr

**Kestr** is a local-first, high-performance background daemon that turns your codebase into a semantic knowledge base. It watches your files in real-time, generates embeddings locally (or via API), and provides an instant semantic search interface for AI agents and tools.

It implements the **Model Context Protocol (MCP)**, making it plug-and-play compatible with tools like **Claude Desktop**, **Gemini CLI**, [**Plexir**](https://github.com/Pomilon/Plexir) and AI-enhanced IDEs.

## Features

*   **Sentry (File Watcher):** Real-time, recursive file monitoring using `inotify` (Linux). Automatically detects changes and queues files for re-indexing.
*   **Talon (Embeddings):** Flexible embedding engine supporting:
    *   **Local ONNX:** Runs `all-MiniLM-L6-v2` locally using ONNX Runtime (No GPU required).
    *   **Ollama:** Connects to a local Ollama instance (default fallback).
    *   **OpenAI:** Uses OpenAI's `text-embedding-3-small` (requires API key).
*   **📚 The Librarian (Search):** Hybrid search engine combining:
    *   **Vector Search:** In-memory HNSW index for semantic understanding.
    *   **Keyword Search:** SQLite-backed fallback for exact matches or low-memory environments.
*   **The Cache:** Persistent SQLite storage for incremental updates (skips unchanged files).
*   **MCP Server:** Native integration with the Model Context Protocol.

## Build Instructions

### Prerequisites
*   Linux (x64)
*   C++20 Compiler (GCC 11+ or Clang 14+)
*   CMake 3.20+
*   SQLite3 (`libsqlite3-dev`)
*   CURL (`libcurl4-openssl-dev`)

### Building
```bash
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

This will produce three executables in `build/bin/`:
*   `kestrd`: The background daemon.
*   `kestr`: The CLI client for control and querying.
*   `kestr-mcp`: The MCP server bridge.

## Configuration

Kestr looks for a configuration file at `~/.config/kestr/config.json`.

**Example `config.json`:**
```json
{
    "memory_mode": "ram",
    "embedding_backend": "ollama",
    "embedding_model": "all-minilm",
    "hybrid_limit": 5000
}
```

### Options
| Option | Values | Description |
|--------|--------|-------------|
| `memory_mode` | `"ram"` | Loads all vectors into RAM for fastest search (Default). |
| | `"hybrid"` | Loads only `hybrid_limit` vectors into RAM. |
| | `"disk"` | Zero-RAM vector mode. Search relies purely on Database Keywords. |
| `embedding_backend` | `"onnx"` | Uses local `model.onnx` files. |
| | `"ollama"` | Uses local Ollama API (Default). |
| | `"openai"` | Uses OpenAI API (Set `OPENAI_API_KEY` env var). |
| `hybrid_limit` | `int` | Number of chunks to keep in RAM if mode is `hybrid`. |

### Local ONNX Setup
To run completely offline without Ollama:
1.  Set `"embedding_backend": "onnx"`.
2.  Download `model.onnx` (e.g., all-MiniLM-L6-v2) and `vocab.txt` (WordPiece) to the directory where you run `kestrd`.

## Usage

### 1. Start the Daemon
Run the daemon in the directory you want to index:
```bash
./bin/kestrd
```
It will perform an initial scan and then watch for changes. Logs and DB are stored in `~/.local/share/kestr/`.

### 2. CLI Commands
Interact with the daemon using the `kestr` client:

```bash
# Check status
kestr status

# Semantic Search
kestr query "How does the file watcher work?"

# Force Re-indexing
kestr reindex

# Stop the daemon
kestr shutdown
```

### 3. Ignoring Files
Create a `.kestr_ignore` file in your project root (syntax similar to `.gitignore`):
```text
node_modules
build
*.log
secret_docs/
```

## MCP Integration

To use Kestr with MCP clients, configure the tool to run `kestr-mcp`.

*Note: Ensure `kestrd` is running in the background for `kestr-mcp` to work.*

## Roadmap & Status
See [ROADMAP.md](ROADMAP.md) for detailed feature tracking.

## License
MIT