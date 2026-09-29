# Sparrow single-signature QR walkthrough

This **Legacy (BIP44), account 0** workflow has been user-tested on a physical
computer, covering descriptor import, receiving and the QR signing round trip.
Sparrow exchanges QR codes directly with Thunder Den.

Use [Thunder Den v0.0.1-preview.4 or later](BUILD.md#download-a-preview) and select
the same Bitcoin network in Sparrow and Thunder Den. Your recovery words and
optional passphrase are entered on Thunder Den; Sparrow receives only the public
descriptor.

## Why BIP44 for this workflow?

Sparrow's **Show QR** export normally removes full previous transactions for
SegWit wallets such as BIP84 to reduce QR size. In a multi-input SegWit-v0
transaction, a malicious coordinator can misstate other inputs' amounts across
repeated signing requests, then combine signatures into a transaction with a
higher fee than the reviews showed. See [BIP174's signer rules and fee-attack explanation](https://github.com/bitcoin/bips/blob/master/bip-0174.mediawiki#signer).

Thunder Den requires full previous transactions unless all inputs are Taproot,
so it can verify the input amounts used to calculate the fee. Sparrow retains
those transactions in its **BIP44** QR export. This guide uses that compatible
export path and keeps Thunder Den's strict checks.

## Create the wallet and receive

1. In Sparrow, create a new wallet and click the camera icon beside **Descriptor**.
2. On Thunder Den, choose **Share wallet setup (descriptor)**, then
   **Legacy (BIP44)** and account **0**. Enter your recovery words and passphrase
   when prompted, review the account and show its QR.
3. Scan that QR with Sparrow and click **Apply** to finish importing the wallet.
4. Use Sparrow's **Receive** tab to obtain a receiving address and receive funds.

Sparrow labels this wallet **read-only** because it cannot sign by itself. It can
still prepare transactions and broadcast them after Thunder Den signs them.

## Send

1. In Sparrow's **Send** tab, enter the destination, amount and fee, then create
   the transaction.
2. Click **Finalize Transaction for Signing**, then **Show QR**. Use **UR** encoding
   if Sparrow offers a format choice.
3. On Thunder Den, choose **Scan a wallet request** and scan Sparrow's QR.
4. Choose **Legacy (BIP44)** and account **0** again, using the same recovery words
   and passphrase as when you exported the descriptor.
5. Review the complete destinations, amounts and fee on Thunder Den. Type `SIGN`
   when ready; Thunder Den displays the signed reply QR.
6. In Sparrow, use **Scan QR** to read that reply. Once Sparrow shows the transaction
   is signed, click **Broadcast Transaction**.

On Thunder Den's result QR, **b / Left** reopens the text review and **Esc — Finish**
returns to the menu. Finishing the display does not broadcast or undo a signature.

## Missing previous transactions

If Thunder Den reports **"Full previous transactions are required outside
all-Taproot spends"**, the exported PSBT lacks data needed for its fee checks.
This is the known BIP84/SegWit QR-export mismatch described above. Selecting BIP44
only at signing time does not convert an existing BIP84 wallet: the address type
and account must match the wallet that received the funds.

**Multisig:** a separate Sparrow walkthrough is planned.
