#!/bin/bash
set -euo pipefail

remove_module_if_present() {
  local mod="$1"
  if command -v modprobe >/dev/null 2>&1; then
    modprobe -r "$mod" >/dev/null 2>&1 || true
  fi
}

if command -v rfkill >/dev/null 2>&1; then
  rfkill block all >/dev/null 2>&1 || true
fi

remove_module_if_present usb_storage
remove_module_if_present uas
remove_module_if_present btusb
remove_module_if_present bluetooth
remove_module_if_present btrtl
remove_module_if_present btintel
remove_module_if_present btbcm
remove_module_if_present cfg80211
remove_module_if_present mac80211

if command -v mount >/dev/null 2>&1; then
  mount -o remount,ro / >/dev/null 2>&1 || true
fi
