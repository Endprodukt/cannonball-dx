#!/usr/bin/env bash

# install.sh
# Install script for CannonBall DX, based on CannonBall-SE by James Pearce
# CannonBall-SE revisions Copyright (c) 2025, James Pearce
# Supports Ubuntu 24.04 and RaspberryPi OS (CLI only)

# Exit on unset vars and errors
set -euo pipefail

# Arrays to record steps and statuses
declare -A STATUS
declare -a STEP_ORDER=()

# Print summary of all steps
function print_summary() {
  echo -e "\n====== Installation Summary ======"
  for step in "${STEP_ORDER[@]}"; do
    printf "%-40s : %s\n" "$step" "${STATUS[$step]:-Skipped}"
  done
}

# Wrapper to run a step, record its status, and abort on failure
function run_step() {
  local name="$1"; shift
  STEP_ORDER+=("$name")
  echo -e "\n--> $name..."
  if "$@"; then
    STATUS["$name"]="Success"
    echo "    [OK] $name completed."
  else
    STATUS["$name"]="Failed"
    echo "    [ERROR] $name failed."
    print_summary
    exit 1
  fi
}

# 1. Confirm with user
echo "Install script for CannonBall DX"
echo "Based on CannonBall-SE by James Pearce"
echo "Supports Ubuntu 24.04 and RaspberryPi OS (CLI only)"
echo ""
read -rp "Do you wish to continue (you will be prompted for sudo)? [y/N] " confirm
STEP_ORDER+=("User confirmation")
if [[ "$confirm" =~ ^[Yy] ]]; then
  STATUS["User confirmation"]="Accepted"
else
  STATUS["User confirmation"]="Aborted by user"
  echo "Aborted. No changes made."
  print_summary
  exit 0
fi

# 2. Determine build threads based on RAM
STEP_ORDER+=("Determine build threads")
{
  mem_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')
  mem_mb=$((mem_kb/1024))
  if (( mem_mb <= 460 )); then threads=1
  elif (( mem_mb <= 920 )); then threads=2
  elif (( mem_mb <= 1380 )); then threads=3
  else threads=4; fi
  export NUMTHREADS=$threads
  STATUS["Determine build threads"]="Using $NUMTHREADS threads ($mem_mb MB RAM)"
  echo "    [INFO] Using $NUMTHREADS build thread(s) based on ${mem_mb}MB RAM."
} || {
  STATUS["Determine build threads"]="Failed"
  echo "    [ERROR] Could not determine RAM size. Defaulting NUMTHREADS=1."
  export NUMTHREADS=1
}

# 3. Update APT
run_step "APT update" sudo apt update -y

# 4. Grant input group access
run_step "Grant input group access" sudo usermod -aG input "$USER"

# 5. Configure udev rules
run_step "Configure udev rules" bash -c '
  sudo tee /etc/udev/rules.d/99-cannonball.rules > /dev/null <<EOF
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", MODE="0666"
KERNEL=="watchdog*", SUBSYSTEM=="watchdog", MODE="0666"
EOF
  sudo udevadm control --reload-rules
  sudo udevadm trigger
'

# 6. Install dependencies
run_step "Install dependencies" sudo apt install -y \
  build-essential git cmake libsdl2-dev libglu1-mesa-dev libmpg123-dev pkg-config alsa-utils libtinyxml2-dev

# 7. Prepare and build CannonBall DX
run_step "Prepare build directories" mkdir -p build roms
run_step "Configure Project with CMake" cmake -S . -B build
run_step "Compile" cmake --build build --parallel $NUMTHREADS
#cd build && cmake ../cmake && make -j"$NUMTHREADS"'
run_step "Create default config file" bash -c 'cp res/config.xml .'

# 8. List and select audio device
env_command="build/cannonball-dx -list-audio-devices"
run_step "List audio devices" bash -c "$env_command"
read -rp "Enter the number of the audio device to use for CannonBall DX [0]: " audio_device
audio_device=${audio_device:-0}
run_step "Configure audio device" bash -c 'sed -i "s|<playback_device>.*</playback_device>|<playback_device>'"$audio_device"'</playback_device>|" config.xml'

# 9. Final summary
print_summary

# 10. Prompt to view the man page
read -rp "Would you like to view the man page now? [y/N] " view_man
STEP_ORDER+=("View man page prompt")
if [[ "$view_man" =~ ^[Yy] ]]; then
  STATUS["View man page prompt"]="Displayed"
  man -l docs/cannonball-dx.6
else
  STATUS["View man page prompt"]="Skipped"
  echo "You can view it later with: man -l docs/cannonball-dx.6"
fi
echo "****************************************************************************************"
echo "IMPORTANT: You must reboot before running CannonBall DX, and remember to copy in the"
echo "           ROMS. After rebooting, start CannonBall DX from this directory using:"
echo ""
echo "           build/cannonball-dx"
echo ""
echo "           If you have no audio, run 'alsa-mixer' and check the <Master> volume is not"
echo "           at zero. Also check the SDL device order hasn't changed using:"
echo ""
echo "           build/cannonball-dx -list-audio-devices"
echo ""
echo "****************************************************************************************"
echo ""

# End of install.sh
