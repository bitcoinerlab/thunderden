# Use Sparrow with Thunder Den

This guide describes the **current source build**, not the published
`v0.0.1-preview.4` image. [Build the current source](BUILD.md#1-build-the-image)
to use the new public-key and PSBT-first signing flows. Use fresh test words and
test coins while validating them.

The older [preview 4 BIP44/account 0 guide](https://github.com/bitcoinerlab/thunderden/blob/v0.0.1-preview.4/docs/SPARROW.md)
was user-tested on a physical computer. The current flow has software checks
using Sparrow 2.5.5; its physical camera round trip is still to be recorded. See
[validation status](STATUS.md#sparrow-integration).

Choose the same Bitcoin network in both applications. Enter recovery words and
the optional passphrase only on Thunder Den. Finish recovery input before
pointing the online computer's camera at its screen.

## Create the wallet and share the public keys

These steps use a **2-of-2 multisig** example. Single-signature setup uses the same
import route; see the note below.

1. In Sparrow, create a wallet. Choose **Policy Type: Multi Signature**, the
   required signatures (for example, **2 of 2**) and the script type.
2. For Thunder Den's keystore, choose **Airgapped Hardware Wallet → SeedSigner →
   Scan**. SeedSigner is the compatible import profile; you can label the
   keystore **Thunder Den** afterwards.
3. On Thunder Den, choose **Share a public key (xpub)** and the matching shortcut:

   | Sparrow script type | Thunder Den shortcut | Public-key path |
   | --- | --- | --- |
   | Native SegWit (P2WSH) | Native SegWit multisig (P2WSH) | `m/48h/coinh/accounth/2h` |
   | Nested SegWit (P2SH-P2WSH) | Nested SegWit multisig (P2SH-P2WSH) | `m/48h/coinh/accounth/1h` |
   | Legacy (P2SH) | Legacy multisig (P2SH) | `m/45h` |

   Coin type is **0** on mainnet and **1** on test networks. BIP48 shortcuts ask
   for the account number; the first account is **0**. Sparrow's legacy shortcut
   uses `m/45h` without an account prompt.
4. Review the full public key, fingerprint and path, then show its QR. Sparrow
   should fill the xpub/tpub, master fingerprint and derivation path. Check all
   three fields.
5. Add the other cosigner(s), then click **Apply**. For a test, the second cosigner
   can be a Sparrow software wallet using a different test seed.

### Single signature: the same import, one key

Choose **Policy Type: Single Signature** and the script type, then follow steps
2–4 with one of these Thunder Den shortcuts:

- **Native SegWit (P2WPKH)**
- **Taproot (P2TR)**
- **Nested SegWit (P2SH-P2WPKH)**
- **Legacy (P2PKH)**

Click **Apply** after importing that one key. Standard single-signature accounts
do not need wallet registration; their transactions go straight to review when
one account matches.

Both setups use the same public-key export and keystore import. Sparrow holds
public information; Thunder Den provides the signatures. Its **xpub / Watch Only
→ camera** route can also import the standard account QR with its fingerprint,
path and script type.

## Receive and sign directly

Use Sparrow's **Receive** tab to receive test coins, then:

1. In Sparrow's **Send** tab, enter the destination, amount and fee, then create
   the transaction.
2. Click **Finalize Transaction for Signing**, then **Show QR**. Select **UR**
   encoding if Sparrow offers a choice.
3. On Thunder Den, choose **Scan a QR code** and scan the transaction.
4. For a new multisig wallet, Thunder Den normally reconstructs the setup from
   the PSBT and shows **Check this multisig wallet**. Review the threshold,
   cosigner keys and paths. Compare other keys with their devices or a trusted
   wallet backup, then type `REGISTER` to approve the setup. **This does not sign
   the transaction yet.** No separate setup scan is needed when the PSBT includes
   all required account keys, origins and scripts.
5. If several approved wallets/accounts can add signatures, choose one. Counts
   in parentheses show its inputs to sign. Several inputs from one wallet do not
   cause an extra chooser.
6. Read any [fee warning](FEES.md), then review destinations, amounts, fee
   information and change. Type `SIGN` to approve.
7. Use Sparrow's **Scan QR** to read the reply. Add any other required signatures,
   then broadcast when the transaction is complete.

An approved multisig wallet stays in RAM for this session, whether its setup came
from the PSBT or a setup QR. The main menu highlights **Loaded wallet:** in orange
only after approval. Later transactions from it can go straight to review. After
logout or reboot, approve the setup again.

Sparrow's compact SegWit QR exports are accepted when sufficient output data is
present. Some multi-input requests need the red fee warning; accepting it does
not verify the missing amounts. Legacy inputs still need full previous transactions.

On a reply QR, **b / Left** reopens the saved review and **Esc — Finish** returns
to the menu. Reopening it does not sign again. If a camera scan fails, retry that
scan while the signed QR is still displayed.

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

## Alternative: load the setup QR

If the PSBT lacks enough setup information, **Wallet setup needed** asks you to
scan the wallet descriptor. You can also load it beforehand to check its first
receiving address:

1. In Sparrow's **Settings**, display the QR beside the completed **Descriptor**.
   Use this setup QR rather than a device-specific export file.
2. On Thunder Den, choose **Scan a QR code** and scan it.
3. Review the type, threshold, public keys and paths. Thunder Den verifies its
   own key; check the others against their devices or your trusted backup.
4. Compare the first receiving address with the same address in Sparrow, then
   type `REGISTER` to approve the setup.

This is a multisig fallback. Thunder Den receives the setup from Sparrow; there
is no separate descriptor-export menu on Thunder Den. The original transaction
stays loaded during an inline setup scan.

The direct importer supports the standard sorted-multisig layouts above, not bare
multisig or MuSig2. Sparrow leaves the address branches implicit in its setup QR,
so Thunder Den explicitly constructs and reviews `/0/*` receiving and `/1/*`
change. Approving another wallet replaces the loaded one; cancelling keeps it.

**No matching signing key** is a different message: check the network, recovery
words, passphrase and supplied key paths rather than registering an unrelated
wallet.

For other public-key requests, **Enter a custom path (advanced)** asks for a path,
then offers **Public-key text** (`[fingerprint/path]xpub`) or **HD key QR (hdkey)**.
Choose the QR format requested by your wallet app. Sparrow's text route preserves
the origin; its standalone `hdkey` scanner does not populate all origin fields.
