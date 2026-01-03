# Project Kestr Roadmap

## Phase 1: Foundation (Completed)
- [x] **Project Skeleton:** CMake build system, cross-platform directory structure.
- [x] **Platform Layer:** Abstractions for File Watching (Sentry), IPC (Bridge), and Client.
- [x] **Core Engine:** Recursive directory scanner, SHA256 hashing, `.kestr_ignore` support.
- [x] **Linux Implementation:** `inotify` watcher and Unix Domain Socket IPC.
- [x] **CLI:** Basic `kestrd` (daemon) and `kestr` (client) with ping/shutdown.

## Phase 2: The Cache (Completed)
**Goal:** State persistence and incremental updates.
- [x] **SQLite Integration:** persistent storage for file metadata and state.
- [x] **Schema Design:**
    - `files` table: path, hash, last_modified, status (indexed/pending).
    - `chunks` table: text content, line ranges.
    - `vectors` table: embedding blobs.
- [x] **Change Detection Logic:** Compare scan results against DB to filter unchanged files.
- [x] **Job Queue:** Dedicated background worker thread for asynchronous embedding.

## Phase 3: Talon (Completed)
**Goal:** Flexible, pluggable embedding generation.
- [x] **Abstract Embedding Interface:** Define a generic `Embedder` class.
- [x] **Local Backend (Default):**
    - Integrated ONNX Runtime.
    - Bundled lightweight model (e.g., `all-MiniLM-L6-v2`).
    - C++ Tokenizer implementation.
- [x] **Remote/API Backend:**
    - Support for external endpoints (HTTP/JSON).
    - **Ollama Integration:** Configurable endpoint for local LLM embedding models.
    - Generic OpenAI-compatible API support.
- [x] **Configuration:** Allow users to switch between "Local (Fast/Private)" and "Remote (Powerful/Custom)" modes.

## Phase 4: The Librarian (Completed)
**Goal:** Efficient retrieval with configurable resource usage.
- [x] **Vector Indexing:** Integration with `hnswlib` for Approximate Nearest Neighbor search.
- [x] **Memory Management Strategies:** Implement a configuration system to handle dataset scale vs. RAM trade-offs.
    - [x] **Mode A: Speed Demon (All-in-RAM):** Load full HNSW index into RAM.
    - [x] **Mode B: Hybrid/Priority:** Load high-priority context into RAM (Implemented via chunk limit).
    - [x] **Mode C: Disk-Optimized:** Minimize RAM usage (Implemented as Keyword-only fallback).
- [x] **Query Pipeline:**
    - User Query -> Embed (using Talon) -> Vector Search (Librarian) -> Result Ranking -> JSON Response.

## Phase 5: Advanced Features & Polish
- [x] **Model Context Protocol (MCP):** Server support implemented in `kestr-mcp`.
- [x] **Interactive CLI:** Commands to manage watchers (`status`, `reindex`) and query.

## Future / Community Contributions
- [ ] **macOS Support:** Implement `FSEvents` for Sentry (untested/unsupported by core team).
- [ ] **Semantic Ranking:** Weight results based on file type (code vs docs) or recency.