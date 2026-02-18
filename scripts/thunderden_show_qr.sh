#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Render input data as terminal QR.

Usage:
  thunderden_show_qr.sh [data]

If data is omitted, one line is read from stdin.

Behavior:
  - If payload fits as one QR frame, show static QR.
  - If payload is too dense, stream animated pMofN frames.

Controls (interactive terminal):
  Enter/q  Exit viewer
  -/+      Smaller/Larger displayed QR (quiet-zone zoom)
  [ ]      Lower/Higher density (smaller/larger chunk per frame)
  s        Return to static-auto mode
EOF
  exit 0
fi

if [ "$#" -gt 0 ]; then
  DATA="$1"
else
  IFS= read -r DATA || true
fi

[ -n "${DATA:-}" ] || die "No data provided"

command -v qrencode >/dev/null 2>&1 || die "Missing command: qrencode"

QR_TYPE="${THUNDERDEN_QR_TYPE:-UTF8}"
QR_ECC="${THUNDERDEN_QR_ECC:-L}"
FRAME_DELAY="${THUNDERDEN_QR_FRAME_DELAY:-0.14}"
CHUNK_STEP="${THUNDERDEN_QR_CHUNK_STEP:-20}"
CHUNK_MIN="${THUNDERDEN_QR_CHUNK_MIN:-80}"
CHUNK_DEFAULT="${THUNDERDEN_QR_CHUNK_DEFAULT:-180}"
CHUNK_MAX="${THUNDERDEN_QR_CHUNK_MAX:-1200}"

MARGIN_MIN=0
MARGIN_MAX=4
MARGIN="${THUNDERDEN_QR_MARGIN:-1}"

PAYLOAD_LEN="${#DATA}"

case "$CHUNK_STEP" in ''|*[!0-9]*) CHUNK_STEP=20 ;; esac
case "$CHUNK_MIN" in ''|*[!0-9]*) CHUNK_MIN=80 ;; esac
case "$CHUNK_DEFAULT" in ''|*[!0-9]*) CHUNK_DEFAULT=180 ;; esac
case "$CHUNK_MAX" in ''|*[!0-9]*) CHUNK_MAX=1200 ;; esac
case "$MARGIN" in ''|*[!0-9]*) MARGIN=1 ;; esac

[ "$CHUNK_STEP" -ge 1 ] || CHUNK_STEP=20
[ "$CHUNK_MIN" -ge 1 ] || CHUNK_MIN=1
[ "$CHUNK_DEFAULT" -ge 1 ] || CHUNK_DEFAULT=1
[ "$CHUNK_MAX" -ge "$CHUNK_MIN" ] || CHUNK_MAX="$CHUNK_MIN"

if [ "$MARGIN" -lt "$MARGIN_MIN" ]; then
  MARGIN="$MARGIN_MIN"
fi
if [ "$MARGIN" -gt "$MARGIN_MAX" ]; then
  MARGIN="$MARGIN_MAX"
fi

if [ "$PAYLOAD_LEN" -lt "$CHUNK_MIN" ]; then
  CHUNK_MIN="$PAYLOAD_LEN"
fi
if [ "$CHUNK_DEFAULT" -lt "$CHUNK_MIN" ]; then
  CHUNK_DEFAULT="$CHUNK_MIN"
fi
if [ "$CHUNK_DEFAULT" -gt "$CHUNK_MAX" ]; then
  CHUNK_DEFAULT="$CHUNK_MAX"
fi
if [ "$CHUNK_DEFAULT" -gt "$PAYLOAD_LEN" ]; then
  CHUNK_DEFAULT="$PAYLOAD_LEN"
fi

PREFER_STATIC=1
CHUNK_SIZE="$CHUNK_DEFAULT"
MODE="static"
FRAME_INDEX=0
NEEDS_RENDER=1
INTERACTIVE=0

declare -a FRAMES=()

can_encode_payload() {
  qrencode -t "$QR_TYPE" -l "$QR_ECC" -m "$MARGIN" -- "$1" >/dev/null 2>&1
}

build_stream_frames_for_size() {
  local size="$1"
  local count=0
  local idx=0
  local start=0
  local part=""
  local frame=""
  local -a tmp=()

  [ "$size" -ge 1 ] || return 1

  count=$(( (PAYLOAD_LEN + size - 1) / size ))
  [ "$count" -ge 1 ] || return 1

  idx=1
  while [ "$idx" -le "$count" ]; do
    start=$(( (idx - 1) * size ))
    part="${DATA:start:size}"
    frame="p${idx}of${count} ${part}"

    if ! can_encode_payload "$frame"; then
      return 1
    fi

    tmp+=("$frame")
    idx=$((idx + 1))
  done

  FRAMES=("${tmp[@]}")
  MODE="stream"
  CHUNK_SIZE="$size"
  return 0
}

build_frames() {
  local size=0

  if [ "$PREFER_STATIC" -eq 1 ] && can_encode_payload "$DATA"; then
    FRAMES=("$DATA")
    MODE="static"
    return 0
  fi

  size="$CHUNK_SIZE"
  if [ "$size" -gt "$CHUNK_MAX" ]; then
    size="$CHUNK_MAX"
  fi
  if [ "$size" -gt "$PAYLOAD_LEN" ]; then
    size="$PAYLOAD_LEN"
  fi
  if [ "$size" -lt "$CHUNK_MIN" ]; then
    size="$CHUNK_MIN"
  fi

  while [ "$size" -ge "$CHUNK_MIN" ]; do
    if build_stream_frames_for_size "$size"; then
      return 0
    fi

    size=$((size - CHUNK_STEP))
    if [ "$size" -lt "$CHUNK_MIN" ]; then
      break
    fi
  done

  die "Payload cannot be rendered in this terminal QR mode."
}

render_current_frame() {
  local current="${FRAMES[$FRAME_INDEX]}"

  if [ "$INTERACTIVE" -eq 1 ]; then
    clear
  fi
  qrencode -t "$QR_TYPE" -l "$QR_ECC" -m "$MARGIN" -- "$current"
  printf '\n'

  if [ "$MODE" = "static" ]; then
    printf 'Mode: static | payload=%s chars\n' "$PAYLOAD_LEN"
  else
    printf 'Mode: stream | frame %s/%s | chunk=%s chars\n' "$((FRAME_INDEX + 1))" "${#FRAMES[@]}" "$CHUNK_SIZE"
  fi

  printf 'Keys: Enter/q done | - smaller + larger | [ less density ] more density | s static-auto\n'
}

apply_density_delta() {
  local delta="$1"
  local requested=0

  PREFER_STATIC=0

  requested=$((CHUNK_SIZE + delta))
  if [ "$requested" -lt "$CHUNK_MIN" ]; then
    requested="$CHUNK_MIN"
  fi
  if [ "$requested" -gt "$CHUNK_MAX" ]; then
    requested="$CHUNK_MAX"
  fi

  CHUNK_SIZE="$requested"
  build_frames
  FRAME_INDEX=0
  NEEDS_RENDER=1
}

handle_key() {
  local key="$1"

  case "$key" in
    $'\n'|$'\r'|q|Q)
      return 1
      ;;
    -)
      if [ "$MARGIN" -gt "$MARGIN_MIN" ]; then
        MARGIN=$((MARGIN - 1))
        NEEDS_RENDER=1
      fi
      ;;
    +|=)
      if [ "$MARGIN" -lt "$MARGIN_MAX" ]; then
        MARGIN=$((MARGIN + 1))
        NEEDS_RENDER=1
      fi
      ;;
    '[')
      apply_density_delta $((-CHUNK_STEP))
      ;;
    ']')
      apply_density_delta "$CHUNK_STEP"
      ;;
    s|S)
      PREFER_STATIC=1
      build_frames
      FRAME_INDEX=0
      NEEDS_RENDER=1
      ;;
    *)
      ;;
  esac

  return 0
}

build_frames

if [ -t 0 ] && [ -t 1 ]; then
  INTERACTIVE=1
else
  FRAME_INDEX=0
  render_current_frame
  exit 0
fi

TTY_STATE="$(stty -g)"
restore_tty() {
  stty "$TTY_STATE" 2>/dev/null || true
}
trap restore_tty EXIT INT TERM

stty -echo -icanon min 0 time 0

while true; do
  local_key=""

  if [ "$NEEDS_RENDER" -eq 1 ]; then
    render_current_frame
    NEEDS_RENDER=0
  fi

  if IFS= read -r -s -n 1 -t "$FRAME_DELAY" local_key; then
    if ! handle_key "$local_key"; then
      break
    fi
    continue
  fi

  if [ "$MODE" = "stream" ]; then
    FRAME_INDEX=$(( (FRAME_INDEX + 1) % ${#FRAMES[@]} ))
    NEEDS_RENDER=1
  fi
done
