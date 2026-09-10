#pragma once

namespace td {
// Boot-time device permissions, then an irreversible drop to uid/gid 1000.
void PrepareSigner();
// Shared by the signer and its freshly executed scanner. Refuses root.
void LockProcess();
// Apply only after opening the camera, before reading any image data.
void ConfineScanner();
}
