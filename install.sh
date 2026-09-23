#!/usr/bin/env bash

# CannonBall DX Linux build helper
# Tested by CI on Ubuntu 24.04. Raspberry Pi OS remains best-effort.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

printf '%s\n' \
  "CannonBall DX Linux setup" \
  "-------------------------" \
  "This installs build/runtime development packages, prepares input access" \
  "and builds CannonBall DX from the current checkout." \
  ""

read -rp "Continue? sudo access will be required. [y/N] " confirm
if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
  echo "Aborted."
  exit 0
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This helper currently supports Debian/Ubuntu-style systems using apt." >&2
  echo "The CMake project itself can still be built manually on other Linux distributions." >&2
  exit 1
fi

# Keep low-memory systems usable while allowing normal desktops to build faster.
if [[ -r /proc/meminfo ]]; then
  mem_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
  mem_mb=$((mem_kb / 1024))
  if (( mem_mb < 768 )); then
    build_threads=1
  elif (( mem_mb < 1536 )); then
    build_threads=2
  elif (( mem_mb < 3072 )); then
    build_threads=3
  else
    build_threads=$(nproc 2>/dev/null || echo 4)
    (( build_threads > 8 )) && build_threads=8
  fi
else
  build_threads=2
fi

echo "Using ${build_threads} build thread(s)."

echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  git \
  cmake \
  ninja-build \
  pkg-config \
  libsdl2-dev \
  libegl1-mesa-dev \
  libgles2-mesa-dev \
  libtinyxml2-dev \
  libmpg123-dev \
  libudev-dev \
  alsa-utils

# Wheels and some controllers expose their force-feedback/input interfaces through
# /dev/input and hidraw. Ubuntu already gives the input group access to many event
# devices; this rule adds the same restricted group access for hidraw without making
# the device world-writable.
echo "Configuring controller/wheel device access..."
sudo groupadd -f input
sudo usermod -aG input "$USER"
sudo tee /etc/udev/rules.d/99-cannonball-dx-input.rules >/dev/null <<'EOF'
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", MODE="0660", GROUP="input", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw || true

mkdir -p build roms

echo "Configuring Release build..."
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_MARCH_NATIVE=ON

echo "Building CannonBall DX..."
cmake --build build --parallel "$build_threads"

if [[ ! -x build/cannonball-dx ]]; then
  echo "Build completed without producing build/cannonball-dx." >&2
  exit 1
fi

printf '\nBuild successful.\n\n'
printf '%s\n' \
  "Put your supported OutRun ROM files or MAME outrun.zip in:" \
  "  $ROOT_DIR/roms/" \
  "" \
  "Start CannonBall DX from this directory with:" \
  "  ./build/cannonball-dx" \
  "" \
  "CannonBall DX creates config.xml automatically on first launch if it is missing." \
  "The game currently uses paths relative to the project/package directory, so launch" \
  "it from here (or use the packaged run.sh from the Linux CI artifact)." \
  ""

# Group membership is only refreshed at the next login. Existing uaccess permissions
# may already be sufficient, but make the requirement explicit for wheel users.
if ! id -nG "$USER" | tr ' ' '\n' | grep -qx input; then
  echo "NOTE: Log out and back in (or reboot) before testing wheel force feedback."
  echo "      Your new 'input' group membership is not active in this session yet."
fi

echo "To inspect SDL audio devices later:"
echo "  ./build/cannonball-dx -list-audio-devices"
echo ""
echo "Linux build setup complete."
