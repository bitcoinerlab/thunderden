# Bitcoin Keeper compatibility

**General signing compatibility is limited by fee verification.** Current Thunder
Den source refuses Keeper's ordinary compact **multi-input SegWit** requests:
they omit previous transactions needed to authenticate input amounts. Allowing
them could hide a much higher real fee; see [fee verification](FEES.md).

This affects both single-signature and multisig wallets. It depends on the number
of transaction inputs, not the number of cosigners.

Still supported:

- Public-key import through **Jade → QR**.
- Wallet registration and receiving-address checks.
- **Single-input SegWit signing**, including multisig.
- Multi-input signing when the PSBT includes sufficient previous transactions.

The limitation is based on [Keeper v2.6.3 source][keeper-qr]: its normal Jade QR
route exports the existing compact PSBT. Switching to Keeper's Krux profile does
not add previous transactions. General compatibility needs a full-data export
route in Keeper.

Earlier successful iOS/testnet4 signing tests predate this stricter policy, which
is not in Thunder Den preview 8. They do not validate the new refusal behavior on
a physical device. See [validation scope](STATUS.md#text-multisig-setup-and-keeper-integration).

## Import a public key

1. Choose the same Bitcoin network in both applications. Keeper's
   **More → App Settings → Network Type → Testnet** corresponds to Thunder Den's
   **Testnet4** selection.
2. For native SegWit multisig, choose **Share a public key (xpub) → Native SegWit
   multisig (P2WSH)** on Thunder Den and the account number, normally **0**.
3. Review and show the account QR. In Keeper's **Jade → QR** flow, scan it as a
   multisig key. Check the fingerprint, derivation and public key.
4. Add the other cosigner keys and create the M-of-N wallet in Keeper.

## Register the wallet and sign a supported request

1. Prepare a supported transaction in Keeper and choose the Jade **QR** signing
   flow. On Thunder Den, choose **Scan a QR code** and scan the **PSBT QR first**.
2. If Thunder Den displays **Wallet setup needed**, choose **scan wallet setup**.
3. In Keeper, tap **Vault details** on the signing screen and scan its registration
   QR with Thunder Den. Use this QR, not **Wallet configuration file → Show QR**.
4. Review the threshold, cosigner fingerprints, paths, public keys and receiving
   address. Compare them with the devices or a trusted backup, then type `REGISTER`.
5. Thunder Den resumes the original PSBT. If fee verification succeeds, review the
   transaction and type `SIGN`. Missing required previous transactions cause an
   error with no override.
6. Scan Thunder Den's reply with Keeper's signed-transaction scanner. Add any
   remaining cosigner signatures to complete the transaction.

You can also scan the **Vault details** registration QR directly from Thunder
Den's main menu before scanning a transaction. `REGISTER` approves spending rules
and keys; it does not sign or authenticate input amounts. The loaded wallet stays
in RAM until replaced or the session ends. Keeper's registration status does not
persist it across Thunder Den reboots.

The text setup importer supports standard **M-of-N P2WSH sorted multisig** with
2–20 keys, subject to Keeper's limits. Single-signature accounts need no multisig
registration.

## Check a receiving address

Load the multisig setup first. Choose **Scan a QR code** on Thunder Den and scan
Keeper's **Receive** QR. Compare the complete matched address before sending.
Payment amounts in the QR are not checked; see
[search controls and limits](PROTOCOL.md#plain-address-checks).

[keeper-qr]: https://github.com/KeeperCommunity/bitcoin-keeper/blob/872cbc4b16ddcb0a8f3b5b1d7ebcde28e06ed096/src/screens/SignTransaction/SignerModals.tsx
