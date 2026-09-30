# Use Sparrow with Thunder Den

This guide describes the **current source build**. The new public-key export,
multisig setup and automatic signing selection are not in the published
`v0.0.1-preview.4` image. [Build the current source](BUILD.md#1-build-the-image)
to use this flow. Use fresh test words and test coins while validating it.

The earlier [preview 4 BIP44/account 0 guide](https://github.com/bitcoinerlab/thunderden/blob/v0.0.1-preview.4/docs/SPARROW.md)
was user-tested on a physical computer. The new flow has software interoperability
checks with Sparrow 2.5.5; a physical camera round trip is still to be recorded.
See [validation status](STATUS.md#sparrow-integration).

Choose the same Bitcoin network in both applications. Enter recovery words and
the optional passphrase only on Thunder Den. Sparrow holds public wallet data.
Finish recovery input before pointing the online computer's camera at its screen.

## Single-signature wallet

1. Create a wallet in Sparrow and click the camera beside **Descriptor**.
2. On Thunder Den, choose **Share a single-signature wallet**, the address type
   and account number. The first account is **0**.
3. Review the account and show its QR. Scan it in Sparrow, then click **Apply**.

Sparrow may label this wallet **read-only**. It can prepare transactions and
broadcast them; Thunder Den supplies the signatures.

## Multisig wallet

### Share each public key

1. In Sparrow, create a wallet. Choose **Policy Type: Multi Signature**, the
   required signatures (for example, **2 of 2**) and the script type.
2. For Thunder Den's keystore, choose **Airgapped Hardware Wallet → SeedSigner →
   Scan**. SeedSigner is the compatible import profile; you can label the
   keystore **Thunder Den** afterwards.
3. On Thunder Den, choose **Share a public key** and the matching shortcut:

   | Sparrow script type | Thunder Den shortcut | Public-key path |
   | --- | --- | --- |
   | Native SegWit (P2WSH) | Native SegWit multisig * (BIP48) | `m/48h/coinh/accounth/2h` |
   | Nested SegWit (P2SH-P2WSH) | Nested SegWit multisig (BIP48) | `m/48h/coinh/accounth/1h` |
   | Legacy (P2SH) | Legacy multisig (P2SH) | `m/45h` |

   The `*` marks the recommended choice for a new multisig wallet. Coin type is
   **0** on mainnet and **1** on test networks. BIP48 shortcuts ask for the account
   number; Sparrow's legacy shortcut uses `m/45h` without an account prompt.
4. Review the fingerprint and path, then show the public-key QR. Sparrow should
   fill the xpub/tpub, master fingerprint and derivation path. Check all three.
5. Add the other cosigner(s), then click **Apply**. For a test, the second cosigner
   can be a Sparrow software wallet using a different test seed.

The same public-key QR also works with Sparrow's **xpub / Watch Only → camera**
route. It contains `[fingerprint/path]xpub`, so no separate format choice is needed.
**Enter a custom path (advanced)** remains available for other public-key requests;
the direct multisig importer supports the standard layouts above.

### Load and check the completed wallet

1. In Sparrow's **Settings**, display the QR beside the completed **Descriptor**.
   Use this setup QR, rather than a device-specific export file.
2. On Thunder Den, choose **Scan a QR code** and scan it.
3. Review the wallet type, threshold, public keys and paths. Compare the other
   keys with their devices or a trusted wallet backup. Thunder Den verifies its
   own key; it cannot establish who owns the other keys.
4. Compare the first receiving address with the same address in Sparrow, then
   type `REGISTER` to approve the setup.

The wallet stays loaded in RAM for this session. Scanning and approving another
setup replaces it. Cancelling a replacement keeps the current wallet. After
logout or reboot, load and approve the setup again.

Sparrow leaves the address branches implicit in this QR. Thunder Den explicitly
uses `/0/*` for receiving and `/1/*` for change, as shown during approval. This
import supports sorted multisig, not bare multisig or MuSig2.

## Receive and sign

Use Sparrow's **Receive** tab. Check the address at the same index against your
approved setup before funding a new wallet. For a single-signature export, the
first receiving address is available in Thunder Den's **Details**.

1. In Sparrow's **Send** tab, enter the destination, amount and fee, then create
   the transaction.
2. Click **Finalize Transaction for Signing**, then **Show QR**. Select **UR**
   encoding if Sparrow offers a choice.
3. On Thunder Den, choose **Scan a QR code**. Matching accounts are verified
   automatically. If a multisig setup is needed, scan and approve it when asked;
   the original transaction stays loaded while you do this.
4. If several wallets can add signatures, choose one. Counts in parentheses show
   the number of inputs it can sign. Several inputs from one wallet do not cause
   an extra chooser.
5. Read any [fee warning](FEES.md), then review the destinations, amounts, fee
   information and change. Type `SIGN` to approve.
6. Scan the reply with Sparrow's **Scan QR**. Add any other required signatures,
   then broadcast when the transaction is complete.

Sparrow's compact SegWit QR exports are accepted when sufficient output data is
present. Some multi-input requests need the red fee warning; accepting it does
not verify the missing amounts. Legacy inputs still need full previous transactions.

On a reply QR, **b / Left** reopens the saved review and **Esc — Finish** returns
to the menu. Reopening the same QR does not sign again. If a camera scan fails,
retry that scan while the signed QR is still displayed.

## Transactions using more than one wallet

Thunder Den signs for **one policy per pass**, preserving existing signatures.
For example, a transaction may spend two inputs from account 0 and one from
account 1:

1. Choose account 0, review and sign.
2. Return the updated PSBT to Sparrow.
3. Display that updated PSBT and scan it again with Thunder Den. Account 1 is now
   the remaining match, so it can go straight to review.
4. Sign and return the reply to Sparrow to complete the transaction.

These are two signing passes for the **same transaction**, with one transaction
fee. A pass signs all eligible inputs for the selected wallet, not just one UTXO.
Back/Esc from a review returns to the chooser when several wallets matched.
