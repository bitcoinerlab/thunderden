#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

status() {
  printf 'Scanner: %s\n' "$*" >&2
}

normalize_payload() {
  local payload="$1"

  payload="${payload//$'\r'/}"
  payload="${payload#QR-Code: }"
  payload="${payload#QR-Code:}"
  printf '%s' "$payload"
}

run_snapshot_preview() {
  local frames="${THUNDERDEN_QR_PREVIEW_FRAMES:-6}"
  local pause_s="${THUNDERDEN_QR_PREVIEW_PAUSE_SEC:-0.25}"
  local frame=1
  local tmp_image=""

  case "$frames" in
    ''|*[!0-9]*)
      status "Invalid THUNDERDEN_QR_PREVIEW_FRAMES value: $frames"
      return 1
      ;;
  esac

  [ "$frames" -gt 0 ] || return 0

  tmp_image="$(mktemp /tmp/thunderden-preview-XXXXXX.jpg)" || {
    status 'Unable to allocate temporary preview image file.'
    return 1
  }

  status "Starting snapshot preview (${frames} frame(s))..."
  status 'Preview helps align the camera before decode begins.'

  while [ "$frame" -le "$frames" ]; do
    if ! v4l2grab -d "$DEVICE" -W 640 -H 480 -o "$tmp_image" >/dev/null 2>&1; then
      status "Snapshot capture failed on $DEVICE (frame $frame/$frames)."
      rm -f "$tmp_image"
      return 1
    fi

    if ! fbv -f -i -y -c "$tmp_image" >/dev/null 2>&1; then
      status "Framebuffer preview failed on frame $frame/$frames."
      rm -f "$tmp_image"
      return 1
    fi

    frame=$((frame + 1))
    sleep "$pause_s"
  done

  rm -f "$tmp_image"
  status 'Preview complete. Switching to QR decode mode...'
  return 0
}

maybe_run_snapshot_preview() {
  [ "${THUNDERDEN_QR_PREVIEW:-1}" = "1" ] || return 0

  if ! command -v v4l2grab >/dev/null 2>&1 || ! command -v fbv >/dev/null 2>&1; then
    status 'Snapshot preview unavailable (missing v4l2grab/fbv); continuing without preview.'
    return 0
  fi

  if [ ! -c /dev/fb0 ] || [ ! -w /dev/fb0 ]; then
    status 'Snapshot preview unavailable (no writable /dev/fb0); continuing without preview.'
    return 0
  fi

  run_snapshot_preview || status 'Snapshot preview failed; continuing with decode-only mode.'
}

scan_psbt_qr() {
  local line=""
  local payload=""
  local preview=""
  local attempt=0

  status "Using camera device: $DEVICE"
  status 'Hold a single-frame base64 PSBT QR in front of the camera.'
  status 'Waiting for payload that starts with cHNidP... (Ctrl+C to cancel)'

  while true; do
    attempt=$((attempt + 1))
    status "Opening camera stream (attempt $attempt)..."

    while IFS= read -r line; do
      [ -n "$line" ] || continue
      payload="$(normalize_payload "$line")"
      [ -n "$payload" ] || continue

      case "$payload" in
        cHNidP*)
          status 'Detected base64 PSBT payload.'
          printf '%s\n' "$payload"
          return 0
          ;;
        *)
          if [ "${#payload}" -gt 36 ]; then
            preview="${payload:0:36}..."
          else
            preview="$payload"
          fi
          status "Decoded non-PSBT QR payload: $preview"
          ;;
      esac
    done < <(zbarcam --raw --quiet --nodisplay "$DEVICE")

    status 'Camera stream ended or failed to open. Retrying in 1 second...'
    sleep 1
  done

  return 0
}

detect_default_device() {
  local dev=""

  for dev in /dev/video*; do
    [ -e "$dev" ] || continue
    [ -c "$dev" ] || continue
    printf '%s\n' "$dev"
    return 0
  done

  return 1
}

DEVICE="${1:-}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Scan camera continuously until a base64 PSBT QR is found.

Usage:
  thunderden_scan_qr.sh [video_device]

Default device: auto-detect first /dev/video*

Environment:
  THUNDERDEN_QR_PREVIEW=0            Disable snapshot preview phase
  THUNDERDEN_QR_PREVIEW_FRAMES=6     Number of preview snapshots
  THUNDERDEN_QR_PREVIEW_PAUSE_SEC=0.25  Delay between preview snapshots
EOF
  exit 0
fi

trap 'die "Scan cancelled"' INT TERM

command -v zbarcam >/dev/null 2>&1 || die "zbarcam is not available in this image"

if [ -z "$DEVICE" ]; then
  DEVICE="$(detect_default_device)" || die "No camera device found under /dev/video*"
fi

[ -c "$DEVICE" ] || die "Camera device is not available: $DEVICE"
[ -r "$DEVICE" ] || die "Camera device is not readable: $DEVICE"

maybe_run_snapshot_preview

scan_psbt_qr || die "No valid base64 PSBT found"
