# Recovery words as QR codes

**Included from `v0.0.1-preview.10`.**

After choosing the Bitcoin network, Thunder Den offers **Scan a seed QR**,
**Enter recovery words**, or **Create a new wallet**. Wallet actions become
available after recovery words and the optional passphrase have been loaded.

Current source also offers **Change network** on this menu (included from preview
11). It returns directly to network selection before a wallet is loaded. After
loading a wallet, end the session first to choose another network.

## Load a backup

Choose **Scan a seed QR** and show the camera your SeedQR or CompactSeedQR backup.
Thunder Den detects the format, closes the camera and explains what it scanned.
Press Enter to use those recovery words, or Esc to discard the scan. The words
are not automatically displayed. Enter your wallet passphrase next if you use one.

After either scanning or typing recovery words, the import review also offers
**Press S to show recovery words and QR on this screen.** The bottom bar abbreviates this
to **S: Show words/QR**. This optional view is labelled **Private recovery backup**.
It shows the same words and their QR together, with **F** for Standard/Compact and
**Q** to hide/show the QR. Enter or Esc returns from the combined view to the
import review. On small screens, Enter advances word pages until the final Back
action; Esc returns immediately, including from a separate QR view. You still
press Enter on the review to proceed. Showing a backup does not require re-entering it,
and reopening the view requires another explicit S. Keys queued during entry or
scanning cannot open the view.

Both QR formats support 12 or 24 English BIP39 words. Manual entry continues to
support 12, 15, 18, 21 or 24 words. These are unencrypted backups: anyone with the
QR has the recovery words. Neither format includes a passphrase or multisig setup.
Keep those backups separately as appropriate for your wallet.
For 15/18/21-word manual imports, **S: Show words** opens a words-only view and
explains that seed QR support is limited to 12/24 words.

The active menu shows the seed fingerprint after applying the optional passphrase,
and any loaded multisig setup. The network remains in the top-right. A fingerprint
is a short identifier, not proof of ownership or a replacement for address checks.

If the main scanner reads another seed QR during a session, **Keep current
session** is the default. To use another seed, explicitly end the session and
scan again in a fresh session. The old signer process exits; no keys, scanned seed
or loaded multisig setup are carried over. Cancelling leaves the current session
unchanged. A seed QR cannot substitute for a requested multisig setup.

## Display a backup

When creating recovery words, the backup screen shows the words beside a
**Standard SeedQR by default**. Press **F** to switch between Standard and Compact,
or **Q** to hide/show the QR. Switching format does not change the words.
The screen supports 12 and 24 words at VGA resolution. Smaller screens retain
paged words with **Q: Seed QR** opening a separate QR view. The written-word
backup check is still required.

The combined view uses the same centered header, orange network label, dividers
and footer as text screens. Twelve words occupy one column; 24 words use two
columns of twelve. The format and next-format shortcut are centered below the QR.

The QR is another copy of the recovery secret, not a public wallet-request QR.
Do not photograph it or scan it with an online wallet app, phone or website.
Use only a trusted offline device. The QR pixels are cleared when leaving the view.
This feature does not keep the recovery phrase throughout the session merely to
offer a later export; backup display happens during creation or at the import
review before the optional passphrase. The phrase is discarded after deriving
the session keys.

## Formats and detection

The implementation follows the
[SeedSigner specification](https://github.com/SeedSigner/seedsigner/blob/dev/docs/seed_qr/README.md):

| | Standard SeedQR | CompactSeedQR |
| --- | --- | --- |
| Payload | 48/96 digits: zero-based, four-digit word indices | 16/32 raw entropy bytes |
| 12-word QR | 25×25 modules | 21×21 modules |
| 24-word QR | 29×29 modules | 25×25 modules |

Sizes use low QR error correction, excluding the four-module quiet border.
Standard uses numeric encoding; Compact uses binary encoding, preserving NUL,
CR and LF bytes. Standard is the default because its numeric payload is easier
to inspect and recover using alternative tools. Compact reduces the size of a
hand-copied backup; it is not encryption.

Standard word indices and the BIP39 checksum are validated by the signer, not
trusted from the scanner. Compact contains only entropy; the BIP39 checksum is
reconstructed, so it cannot independently establish that a scan is the intended
backup. Compact also has no identifying prefix: arbitrary 16/32-byte QR data can
be interpreted as recovery words. A valid 32-character Bitcoin Base58 address
takes precedence. No seed can replace an in-progress multipart request or change
the active session automatically.

The scanner's raw seed records and the signer's mnemonic/QR-module buffers use
wiping storage. The scanner exits before confirmation; the framebuffer is cleared
before returning to text mode. These measures do not establish erasure of every
decoder/encoder-library temporary or physical-memory copy. Session changes retain
the existing full-process exit boundary; no persistent seed handoff is introduced.

Tests cover all nine published SeedSigner vectors, numeric/binary optical round
trips, malformed inputs, address precedence, hostile pipe records, session choices,
passphrases, fingerprint spacing, and readable VGA/high-DPI backup layouts.
