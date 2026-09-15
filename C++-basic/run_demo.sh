#!/usr/bin/env bash
# Interactive demo runner for C++-basic: builds the project, then walks you
# through choosing a protocol, window size, impairment level, and input file
# before running one sender/receiver transfer over loopback and verifying the
# output is byte-identical to the input.
#
# Usage: ./run_demo.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PORT="${PORT:-5000}"

# --- helper: prompt with a numbered menu, echo the chosen value ---------
choose() {
  local prompt="$1"
  shift
  local options=("$@")
  echo "$prompt" >&2
  local PS3="> "
  select opt in "${options[@]}"; do
    if [ -n "${opt:-}" ]; then
      echo "$opt"
      return 0
    fi
    echo "Invalid choice, try again." >&2
  done
}

echo "==> Building (make all)"
make all
echo

# --- 1. protocol ----------------------------------------------------------
protocol_choice=$(choose "Choose a protocol:" \
  "stop-and-wait" \
  "go-back-n" \
  "selective-repeat")
PROTOCOL="$protocol_choice"

# --- 2. window (skip for stop-and-wait, which is always 1) ---------------
WINDOW=1
if [ "$PROTOCOL" != "stop-and-wait" ]; then
  max_window=255
  [ "$PROTOCOL" = "selective-repeat" ] && max_window=128
  window_choice=$(choose "Choose a window size (Go-Back-N: 1-255, Selective Repeat: 1-128):" \
    "4" "8" "16" "32" "Custom")
  if [ "$window_choice" = "Custom" ]; then
    read -r -p "Enter window size (1-$max_window): " WINDOW
  else
    WINDOW="$window_choice"
  fi
fi

# --- 3. impairment ----------------------------------------------------------
impairment_choice=$(choose "Choose a channel impairment level (applied to both DATA and ACK paths):" \
  "Clean (0.0 / 0.0)" \
  "Light (0.1 error / 0.1 loss)" \
  "Moderate (0.2 error / 0.2 loss)" \
  "Heavy (0.4 error / 0.4 loss)" \
  "Custom")
case "$impairment_choice" in
  "Clean (0.0 / 0.0)") ERROR_PROB=0.0; LOSS_PROB=0.0 ;;
  "Light (0.1 error / 0.1 loss)") ERROR_PROB=0.1; LOSS_PROB=0.1 ;;
  "Moderate (0.2 error / 0.2 loss)") ERROR_PROB=0.2; LOSS_PROB=0.2 ;;
  "Heavy (0.4 error / 0.4 loss)") ERROR_PROB=0.4; LOSS_PROB=0.4 ;;
  "Custom")
    read -r -p "Enter error probability (0.0-1.0): " ERROR_PROB
    read -r -p "Enter loss probability (0.0-1.0): " LOSS_PROB
    ;;
esac

# --- 4. input file ----------------------------------------------------------
default_input="test_data/input.txt"
input_choice=$(choose "Choose an input file:" \
  "$default_input" \
  "Custom path")
if [ "$input_choice" = "Custom path" ]; then
  read -r -p "Enter path to input file: " INPUT
else
  INPUT="$input_choice"
fi

if [ ! -f "$INPUT" ]; then
  echo "error: input file not found: $INPUT" >&2
  exit 1
fi

# --- run ----------------------------------------------------------
mkdir -p output_files
OUTPUT="output_files/received_${PROTOCOL}.bin"

WINDOW_ARGS=()
if [ "$PROTOCOL" != "stop-and-wait" ]; then
  WINDOW_ARGS=(--window "$WINDOW")
fi

echo
echo "==> protocol=$PROTOCOL window=$WINDOW error=$ERROR_PROB loss=$LOSS_PROB input=$INPUT"
echo "==> Starting receiver (port=$PORT output=$OUTPUT)"
./build/receiver --protocol "$PROTOCOL" --port "$PORT" --output "$OUTPUT" "${WINDOW_ARGS[@]}" \
  --ack-error "$ERROR_PROB" --ack-loss "$LOSS_PROB" --seed 2 &
RECEIVER_PID=$!

trap 'kill "$RECEIVER_PID" 2>/dev/null || true' EXIT
sleep 0.3

echo "==> Running sender"
./build/sender --protocol "$PROTOCOL" --port "$PORT" --input "$INPUT" "${WINDOW_ARGS[@]}" \
  --data-error "$ERROR_PROB" --data-loss "$LOSS_PROB" --seed 1

wait "$RECEIVER_PID"
trap - EXIT

echo "==> Verifying output"
if cmp -s "$INPUT" "$OUTPUT"; then
  echo "OK: $OUTPUT is byte-identical to $INPUT"
else
  echo "MISMATCH: $OUTPUT differs from $INPUT" >&2
  exit 1
fi
