# Use Bitcoin Keeper with Thunder Den

Use [Thunder Den v0.0.1-preview.6](https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.6)
or later. The user reported successful multisig
signing with Keeper on iOS/testnet4 using the text-setup development image.
The main-scanner preload option was added afterwards and has automated coverage.
See [validation scope](STATUS.md#text-multisig-setup-and-keeper-integration).

## Select Jade and QR in Keeper

**In Keeper, add Thunder Den as a Jade signer and select QR communication.**
Keeper's Jade import accepts Thunder Den's standard `ur:crypto-account` public-key
QR. The signer code is wallet-independent; Jade is the compatible Keeper profile.

For a native SegWit multisig wallet:

1. Choose the same Bitcoin network in Keeper and Thunder Den.
   For testnet4, Keeper's **More → App Settings → Network Type → Testnet** option
   corresponds to Thunder Den's **Testnet4** selection.
2. On Thunder Den, choose **Share a public key (xpub) → Native SegWit multisig
   (P2WSH)** and the account number, normally **0**.
3. Review and show the account QR. In Keeper's **Jade → QR** flow, scan it as a
   multisig key. Check the fingerprint, derivation and public key.
4. Add the other cosigner keys and create the M-of-N wallet in Keeper.

The account path is `m/48h/0h/accounth/2h` on mainnet or
`m/48h/1h/accounth/2h` on test networks. Standard account export is sufficient;
the advanced text-xpub workaround is unnecessary with Keeper's Jade profile.

## Sign: PSBT first, then Vault details

On the first transaction of a session, Keeper's Jade QR PSBT may omit the global
account xpubs needed to reconstruct the complete multisig wallet. **Wallet setup
needed** is the expected next step.

1. In Keeper, prepare the transaction and choose the Jade **QR** signing flow.
2. On Thunder Den, choose **Scan a QR code** and scan Keeper's **PSBT QR first**.
3. When Thunder Den displays **Wallet setup needed**, choose **scan wallet setup**.
4. On Keeper's signing screen, tap **Vault details**. This opens the registration
   QR containing the wallet configuration. Scan that QR with Thunder Den.
5. Review the threshold, cosigner fingerprints, paths and public keys, and the
   receiving address. Compare the cosigner keys with their devices or a trusted
   backup. Type **REGISTER** to approve the wallet for this session.
6. Thunder Den resumes the original PSBT automatically. Review any
   [fee warning](FEES.md), then the transaction, and type **SIGN** to approve.
7. In Keeper, use its signed-transaction scanner to read Thunder Den's reply QR.
   Add any remaining cosigner signatures in Keeper to complete the transaction.

`REGISTER` approves the wallet setup; it does not sign the transaction. The PSBT
stays loaded during registration, so it does not need to be scanned again.

Keeper's **Vault details** QR in this flow contains a public text multisig setup
inside `ur:bytes`. Thunder Den converts it into its validated descriptor policy.
This is different from Keeper's separate **Wallet configuration file → Show QR**
export of raw descriptor text: use the registration QR from **Vault details**.

## Alternative: load the wallet before scanning the PSBT

If the same registration QR is already displayed in Keeper, choose **Scan a QR
code** on Thunder Den's main menu and scan it directly. Review and type `REGISTER`.
The main menu then shows **Loaded wallet: Native SegWit multisig (M of N)** in
orange. Scan Keeper's PSBT next; a matching transaction can go straight to review.

One approved wallet stays in RAM for the session. Approving a replacement changes
that wallet; cancelling or invalid setup preserves the old one. **End session**
or reboot clears it, so a new session requires registration again. Keeper marking
a signer as registered does not persist the wallet inside Thunder Den.

## Supported setup

The text importer supports standard **M-of-N P2WSH sorted multisig**, with
`1 <= M <= N` and 2–20 keys, subject to the coordinator's own limits. It is a
general [text setup format](PROTOCOL.md#text-multisig-setup), not a Keeper-specific
parser. Single-signature accounts use the ordinary account-signing flow and do
not require this multisig registration.
