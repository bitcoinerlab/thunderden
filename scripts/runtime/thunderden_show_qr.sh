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
  - Auto mode uses one standard display profile.
  - In auto mode, show static if reasonable; otherwise stream pMofN frames.
  - Optional override can show full static QR if it fits screen.

Controls (interactive terminal):
  Enter/q  Exit viewer
  p        Full static preview (if it fits)
  o        Back to auto profile
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

QR_TYPE="UTF8i"
QR_ECC="L"
FRAME_DELAY="0.14"

STANDARD_MAX_VERSION="6"
CHUNK_STEP="20"
CHUNK_MIN="80"
CHUNK_DEFAULT="180"
CHUNK_MAX="1200"
MARGIN="2"

MAX_QR_VERSION=40
PAYLOAD_LEN="${#DATA}"

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

INTERACTIVE=0
TTY_STATE=""

VIEW_MODE="auto" # auto | full-static
RENDER_KIND="static" # static | stream
FRAME_VERSION=1
FULL_STATIC_VERSION=0
FULL_STATIC_FITS=0
CHUNK_SIZE="$CHUNK_DEFAULT"
FRAME_INDEX=0
NEEDS_RENDER=1
STATUS_LINE=""
FORCE_CLEAR=1
FOOTER_DIRTY=1

declare -a FRAMES=()

qrencode_base() {
  local payload="$1"
  local version="${2:-0}"

  if [ "$version" -gt 0 ]; then
    qrencode -t "$QR_TYPE" -l "$QR_ECC" -m "$MARGIN" -v "$version" --strict-version -- "$payload"
  else
    qrencode -t "$QR_TYPE" -l "$QR_ECC" -m "$MARGIN" -- "$payload"
  fi
}

can_encode_payload() {
  local payload="$1"
  local version="${2:-0}"

  qrencode_base "$payload" "$version" >/dev/null 2>&1
}

find_min_version_up_to() {
  local payload="$1"
  local limit="$2"
  local low=1
  local high="$limit"
  local mid=0

  if ! can_encode_payload "$payload" "$high"; then
    return 1
  fi

  while [ "$low" -lt "$high" ]; do
    mid=$(( (low + high) / 2 ))
    if can_encode_payload "$payload" "$mid"; then
      high="$mid"
    else
      low=$((mid + 1))
    fi
  done

  printf '%s' "$low"
  return 0
}

fits_screen() {
  local version="$1"
  local tty_size=""
  local tty_rows=0
  local tty_cols=0
  local modules=0
  local rows=0
  local cols=0
  local footer_rows=4
  local max_cols=0

  [ "$INTERACTIVE" -eq 1 ] || return 0

  tty_size="$(stty size < /dev/tty 2>/dev/null || true)"
  tty_rows="${tty_size%% *}"
  tty_cols="${tty_size##* }"

  case "$tty_rows" in ''|*[!0-9]*) return 1 ;; esac
  case "$tty_cols" in ''|*[!0-9]*) return 1 ;; esac

  modules=$((17 + 4 * version + 2 * MARGIN))
  rows=$(( (modules + 1) / 2 ))
  cols="$modules"

  max_cols=$((tty_cols - 1))
  [ "$max_cols" -ge 1 ] || return 1

  [ "$cols" -le "$max_cols" ] && [ $((rows + footer_rows)) -le "$tty_rows" ]
}

update_full_static_capability() {
  if [ "$FULL_STATIC_VERSION" -eq 0 ]; then
    FULL_STATIC_VERSION="$(find_min_version_up_to "$DATA" "$MAX_QR_VERSION")" || FULL_STATIC_VERSION=0
  fi

  if [ "$FULL_STATIC_VERSION" -gt 0 ] && fits_screen "$FULL_STATIC_VERSION"; then
    FULL_STATIC_FITS=1
  else
    FULL_STATIC_FITS=0
  fi
}

build_stream_frames_for_size() {
  local size="$1"
  local max_version="$2"
  local count=0
  local idx=0
  local start=0
  local part=""
  local frame=""
  local sample=""
  local v=0
  local -a tmp=()

  [ "$size" -ge 1 ] || return 1

  count=$(( (PAYLOAD_LEN + size - 1) / size ))
  [ "$count" -ge 1 ] || return 1

  part="${DATA:0:size}"
  sample="p${count}of${count} ${part}"

  v="$(find_min_version_up_to "$sample" "$max_version")" || return 1
  if ! fits_screen "$v"; then
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
  RENDER_KIND="stream"
  FRAME_VERSION="$v"
  CHUNK_SIZE="$size"
  FRAME_INDEX=0
  return 0
}

build_stream_auto_with_limit() {
  local max_version="$1"
  local size=0

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
    if build_stream_frames_for_size "$size" "$max_version"; then
      return 0
    fi
    size=$((size - CHUNK_STEP))
    if [ "$size" -lt "$CHUNK_MIN" ]; then
      break
    fi
  done

  return 1
}

build_auto_view() {
  update_full_static_capability

  if [ "$FULL_STATIC_VERSION" -gt 0 ] &&
     [ "$FULL_STATIC_VERSION" -le "$STANDARD_MAX_VERSION" ] &&
     [ "$FULL_STATIC_FITS" -eq 1 ]; then
    FRAMES=("$DATA")
    RENDER_KIND="static"
    FRAME_VERSION="$FULL_STATIC_VERSION"
    CHUNK_SIZE="$CHUNK_DEFAULT"
    FRAME_INDEX=0
    return 0
  fi

  if build_stream_auto_with_limit "$STANDARD_MAX_VERSION"; then
    return 0
  fi

  if build_stream_auto_with_limit "$MAX_QR_VERSION"; then
    STATUS_LINE="Auto stream uses denser QR due payload size."
    return 0
  fi

  if [ "$FULL_STATIC_VERSION" -gt 0 ] && [ "$FULL_STATIC_FITS" -eq 1 ]; then
    FRAMES=("$DATA")
    RENDER_KIND="static"
    FRAME_VERSION="$FULL_STATIC_VERSION"
    CHUNK_SIZE="$CHUNK_DEFAULT"
    FRAME_INDEX=0
    STATUS_LINE="Auto fallback: using full static QR."
    return 0
  fi

  return 1
}

build_full_static_view() {
  update_full_static_capability

  if [ "$FULL_STATIC_VERSION" -le 0 ]; then
    return 1
  fi
  if [ "$FULL_STATIC_FITS" -ne 1 ]; then
    return 1
  fi

  FRAMES=("$DATA")
  RENDER_KIND="static"
  FRAME_VERSION="$FULL_STATIC_VERSION"
  FRAME_INDEX=0
  return 0
}

render_view() {
  local payload="${FRAMES[$FRAME_INDEX]}"

  if [ "$INTERACTIVE" -eq 1 ]; then
    if [ "$FORCE_CLEAR" -eq 1 ]; then
      printf '\033[2J\033[H' > /dev/tty
      FORCE_CLEAR=0
      FOOTER_DIRTY=1
    else
      printf '\033[H' > /dev/tty
    fi
  fi

  qrencode_base "$payload" "$FRAME_VERSION"

  if [ "$INTERACTIVE" -eq 1 ] &&
     [ "$VIEW_MODE" = "auto" ] &&
     [ "$RENDER_KIND" = "stream" ] &&
     [ "$FOOTER_DIRTY" -eq 0 ]; then
    return
  fi

  printf '\n'

  if [ "$VIEW_MODE" = "full-static" ]; then
    printf 'Mode: full static preview | qr-v=%s\n' "$FRAME_VERSION"
  elif [ "$RENDER_KIND" = "static" ]; then
    printf 'Mode: auto static | qr-v=%s\n' "$FRAME_VERSION"
  else
    printf 'Mode: auto stream | chunk=%s chars | qr-v=%s\n' "$CHUNK_SIZE" "$FRAME_VERSION"
  fi

  if [ "$VIEW_MODE" = "full-static" ]; then
    printf 'Keys: Enter/q done | o back to auto\n'
  else
    printf 'Keys: Enter/q done | p full static preview\n'
  fi

  if [ -n "$STATUS_LINE" ]; then
    printf '%s\n' "$STATUS_LINE"
  else
    printf '\n'
  fi

  FOOTER_DIRTY=0
}

read_key() {
  local key=""

  if ! IFS= read -r -s -N 1 -t "$FRAME_DELAY" key < /dev/tty; then
    return 1
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
    p|P)
      if [ "$VIEW_MODE" = "full-static" ]; then
        STATUS_LINE="Already showing full static preview."
        NEEDS_RENDER=1
        FOOTER_DIRTY=1
        return 0
      fi
      if build_full_static_view; then
        VIEW_MODE="full-static"
        STATUS_LINE=""
        FORCE_CLEAR=1
      else
        STATUS_LINE="Full static preview is not available (too dense or too large for this screen)."
      fi
      FRAME_INDEX=0
      NEEDS_RENDER=1
      FOOTER_DIRTY=1
      ;;
    o|O)
      if [ "$VIEW_MODE" = "auto" ]; then
        STATUS_LINE="Already in auto profile."
        NEEDS_RENDER=1
        FOOTER_DIRTY=1
        return 0
      fi
      if build_auto_view; then
        VIEW_MODE="auto"
        STATUS_LINE=""
        FORCE_CLEAR=1
      else
        STATUS_LINE="Unable to return to auto profile with current settings."
      fi
      FRAME_INDEX=0
      NEEDS_RENDER=1
      FOOTER_DIRTY=1
      ;;
    *)
      ;;
  esac

  return 0
}

if [ -t 1 ] && TTY_STATE="$(stty -g < /dev/tty 2>/dev/null)"; then
  INTERACTIVE=1
fi

build_auto_view || die "Payload cannot be rendered in this terminal QR mode."

if [ "$INTERACTIVE" -eq 0 ]; then
  render_view
  exit 0
fi

restore_tty() {
  stty "$TTY_STATE" < /dev/tty 2>/dev/null || true
  printf '\033[?25h' > /dev/tty 2>/dev/null || true
}
trap restore_tty EXIT INT TERM

stty -echo -icanon min 0 time 0 < /dev/tty
printf '\033[?25l' > /dev/tty

while true; do
  if [ "$NEEDS_RENDER" -eq 1 ]; then
    render_view
    NEEDS_RENDER=0
  fi

  KEY_RESULT=""
  if read_key; then
    if ! handle_key "$KEY_RESULT"; then
      break
    fi
    continue
  fi

  if [ "$VIEW_MODE" = "auto" ] && [ "$RENDER_KIND" = "stream" ] && [ "${#FRAMES[@]}" -gt 1 ]; then
    FRAME_INDEX=$(( (FRAME_INDEX + 1) % ${#FRAMES[@]} ))
    NEEDS_RENDER=1
  fi
done
