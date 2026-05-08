#!/bin/bash

set -e

echo "🦅 Installing Kestr..."

# 1. Check dependencies (Basic check)
for cmd in cmake curl git; do
    if ! command -v $cmd &> /dev/null; then
        echo "Error: $cmd is required."
        exit 1
    fi
done

# 2. Build
echo "🔨 Building..."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)
cd ..

# 3. Install Binaries
INSTALL_BIN="$HOME/.local/bin"
echo "📦 Installing binaries to $INSTALL_BIN..."
mkdir -p "$INSTALL_BIN"
cp build/bin/kestrd "$INSTALL_BIN/"
cp build/bin/kestr "$INSTALL_BIN/"
cp build/bin/kestr-mcp "$INSTALL_BIN/"

# 4. Install Config
CONFIG_DIR="$HOME/.config/kestr"
echo "⚙️  Setting up configuration in $CONFIG_DIR..."
mkdir -p "$CONFIG_DIR"
if [ ! -f "$CONFIG_DIR/config.json" ]; then
    echo '{
    "memory_mode": "ram",
    "hybrid_limit": 5000,
    "embedding_backend": "ollama",
    "embedding_model": "nomic-embed-text",
    "watch_paths": []
}' > "$CONFIG_DIR/config.json"
    echo "   Created default config.json"
else
    echo "   Existing config.json found, skipping."
fi

# 5. Data Directory
DATA_DIR="$HOME/.local/share/kestr"
echo "💾 Preparing data directory: $DATA_DIR"
mkdir -p "$DATA_DIR"

# 6. Systemd Service (Optional)
read -p "🖥️  Do you want to install Kestr as a systemd user service? (y/N) " response
if [[ "$response" =~ ^[Yy]$ ]]; then
    SERVICE_DIR="$HOME/.config/systemd/user"
    echo "   Setting up systemd service in $SERVICE_DIR..."
    mkdir -p "$SERVICE_DIR"
    cp kestr.service "$SERVICE_DIR/"
    systemctl --user daemon-reload
    echo "   Service installed."
    echo "   -> Start: systemctl --user start kestr"
    echo "   -> Enable at boot: systemctl --user enable kestr"
else
    echo "   Skipping systemd setup."
fi

echo "✅ Installation complete!"
echo "🚀 Start the daemon and visit http://localhost:8080 for the observability dashboard."
