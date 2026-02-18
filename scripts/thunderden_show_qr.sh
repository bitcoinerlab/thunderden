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
  w/s      Larger/Smaller displayed QR
  a/d      Lower/Higher density (smaller/larger chunk per frame)
  arrows   Up/Down = size, Left/Right = density
  r        Return to static-auto mode
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
ZOOM_MIN=0
ZOOM_MAX=4
ZOOM="${THUNDERDEN_QR_ZOOM:-0}"

MARGIN="${THUNDERDEN_QR_MARGIN:-1}"

PAYLOAD_LEN="${#DATA}"

case "$CHUNK_STEP" in ''|*[!0-9]*) CHUNK_STEP=20 ;; esac
case "$CHUNK_MIN" in ''|*[!0-9]*) CHUNK_MIN=80 ;; esac
case "$CHUNK_DEFAULT" in ''|*[!0-9]*) CHUNK_DEFAULT=180 ;; esac
case "$CHUNK_MAX" in ''|*[!0-9]*) CHUNK_MAX=1200 ;; esac
case "$ZOOM" in ''|*[!0-9]*) ZOOM=0 ;; esac
case "$MARGIN" in ''|*[!0-9]*) MARGIN=1 ;; esac

[ "$CHUNK_STEP" -ge 1 ] || CHUNK_STEP=20
[ "$CHUNK_MIN" -ge 1 ] || CHUNK_MIN=1
[ "$CHUNK_DEFAULT" -ge 1 ] || CHUNK_DEFAULT=1
[ "$CHUNK_MAX" -ge "$CHUNK_MIN" ] || CHUNK_MAX="$CHUNK_MIN"
[ "$ZOOM" -ge "$ZOOM_MIN" ] || ZOOM="$ZOOM_MIN"
[ "$ZOOM" -le "$ZOOM_MAX" ] || ZOOM="$ZOOM_MAX"

if [ "$MARGIN" -lt 0 ]; then
  MARGIN=0
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

scale_ascii_qr() {
  local factor="$1"

  if [ "$factor" -le 1 ]; then
    cat
    return 0
  fi

  awk -v f="$factor" '
    {
      out = ""
      for (i = 1; i <= length($0); i++) {
        c = substr($0, i, 1)
        for (j = 0; j < f; j++) {
          out = out c
        }
      }
      for (k = 0; k < f; k++) {
        print out
      }
    }
  '
}

render_qr_payload() {
  local payload="$1"

  if [ "$ZOOM" -eq 0 ]; then
    qrencode -t "$QR_TYPE" -l "$QR_ECC" -m "$MARGIN" -- "$payload"
    return 0
  fi

  qrencode -t ASCII -l "$QR_ECC" -m "$MARGIN" -- "$payload" | scale_ascii_qr "$ZOOM"
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

  # Fast capacity check once using the longest header width.
  part="${DATA:0:size}"
  frame="p${count}of${count} ${part}"
  if ! can_encode_payload "$frame"; then
    return 1
  fi

  idx=1
  while [ "$idx" -le "$count" ]; do
    start=$(( (idx - 1) * size ))
    part="${DATA:start:size}"
    frame="p${idx}of${count} ${part}"

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
  render_qr_payload "$current"
  printf '\n'

  if [ "$MODE" = "static" ]; then
    printf 'Mode: static | payload=%s chars | size=%s\n' "$PAYLOAD_LEN" "$ZOOM"
  else
    printf 'Mode: stream | frame %s/%s | chunk=%s chars | size=%s\n' "$((FRAME_INDEX + 1))" "${#FRAMES[@]}" "$CHUNK_SIZE" "$ZOOM"
  fi

  printf 'Keys: Enter/q done | w/s size | a/d density | arrows supported | r static-auto\n'
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

apply_zoom_delta() {
  local delta="$1"
  local requested=0

  requested=$((ZOOM + delta))
  if [ "$requested" -lt "$ZOOM_MIN" ]; then
    requested="$ZOOM_MIN"
  fi
  if [ "$requested" -gt "$ZOOM_MAX" ]; then
    requested="$ZOOM_MAX"
  fi

  if [ "$requested" -ne "$ZOOM" ]; then
    ZOOM="$requested"
    NEEDS_RENDER=1
  fi
}

read_key() {
  local key=""
  local a=""
  local b=""

  if ! IFS= read -r -s -n 1 -t "$FRAME_DELAY" key < /dev/tty; then
    return 1
  fi

  if [ "$key" = $'\033' ]; then
    if IFS= read -r -s -n 1 -t 0.02 a < /dev/tty && [ "$a" = "[" ]; then
      if IFS= read -r -s -n 1 -t 0.02 b < /dev/tty; then
        case "$b" in
          A) KEY_RESULT="UP" ; return 0 ;;
          B) KEY_RESULT="DOWN" ; return 0 ;;
          C) KEY_RESULT="RIGHT" ; return 0 ;;
          D) KEY_RESULT="LEFT" ; return 0 ;;
          *) ;;
        esac
      fi
    fi
  fi

  KEY_RESULT="$key"
  return 0
}

handle_key() {
  local key="$1"

  case "$key" in
    $'\n'|$'\r'|q|Q)
      return 1
      ;;
    w|W|UP)
      apply_zoom_delta 1
      ;;
    s|S|DOWN)
      apply_zoom_delta -1
      ;;
    a|A|LEFT)
      apply_density_delta $((-CHUNK_STEP))
      ;;
    d|D|RIGHT)
      apply_density_delta "$CHUNK_STEP"
      ;;
    r|R)
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

TTY_STATE=""

if [ -t 1 ] && TTY_STATE="$(stty -g < /dev/tty 2>/dev/null)"; then
  INTERACTIVE=1
else
  FRAME_INDEX=0
  render_current_frame
  exit 0
fi

restore_tty() {
  stty "$TTY_STATE" < /dev/tty 2>/dev/null || true
}
trap restore_tty EXIT INT TERM

stty -echo -icanon min 0 time 0 < /dev/tty

while true; do
  local_key=""

  if [ "$NEEDS_RENDER" -eq 1 ]; then
    render_current_frame
    NEEDS_RENDER=0
  fi

  KEY_RESULT=""
  if read_key; then
    if ! handle_key "$KEY_RESULT"; then
      break
    fi
    continue
  fi

  if [ "$MODE" = "stream" ]; then
    FRAME_INDEX=$(( (FRAME_INDEX + 1) % ${#FRAMES[@]} ))
    NEEDS_RENDER=1
  fi
done
