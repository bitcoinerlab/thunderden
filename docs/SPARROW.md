# Use Sparrow with Thunder Den

Use **Sparrow 2.5.5 or later**. For each Thunder Den keystore, choose
**Airgapped Hardware Wallet → Krux → Scan**, for both single-signature and
multisig wallets. Krux uses compatible QR formats and includes the previous
transactions needed to verify fees. You can label the keystore **Thunder Den**.

Choose the same Bitcoin network in both applications. Enter recovery words and
the optional passphrase only on Thunder Den. Finish recovery input before
pointing the online computer's camera at its screen.

## Create the wallet and share the public keys

1. In Sparrow, create a wallet and choose the policy and script type. For multisig,
   choose the required signatures, for example **2 of 2**.
2. For Thunder Den's keystore, choose **Airgapped Hardware Wallet → Krux → Scan**.
3. On Thunder Den, choose **Share a public key (xpub)** and the matching shortcut:

   | Sparrow script type | Thunder Den shortcut |
   | --- | --- |
   | Native SegWit (P2WSH) | Native SegWit multisig (P2WSH) |
   | Nested SegWit (P2SH-P2WSH) | Nested SegWit multisig (P2SH-P2WSH) |
   | Legacy (P2SH) | Legacy multisig (P2SH) |

   For single signature, choose **Native SegWit (P2WPKH)**, **Taproot (P2TR)**,
   **Nested SegWit (P2SH-P2WPKH)** or **Legacy (P2PKH)** instead.
   The first account is normally **0**. Legacy multisig uses `m/45h` without an
   account prompt; SegWit multisig uses BIP48 paths.
4. Review the public key, fingerprint and path, then show its QR. Scan it in Sparrow
   and check all three imported fields.
5. Add any other cosigners, then click **Apply**.

## Receive

For multisig, [load the setup](#alternative-load-the-setup-qr) first. Choose
**Scan a QR code** on Thunder Den and scan the QR in Sparrow's **Receive** tab.
Confirm the account number (normally 0), then compare the complete matched address
with the destination before sending. **Esc** stops the search; see
[search controls and limits](PROTOCOL.md#plain-address-checks).

## Sign

1. In Sparrow's **Send** tab, enter the destination, amount and fee, then create
   the transaction.
2. Click **Finalize Transaction for Signing**, then **Show QR**. Select **UR**
   encoding if offered.
3. On Thunder Den, choose **Scan a QR code** and scan the transaction.
4. For a new multisig wallet, review the setup when prompted: threshold, cosigner
   keys and paths. Compare other keys with their devices or a trusted wallet
   backup, then type `REGISTER`. This approves the setup, not the transaction.
   If the PSBT lacks setup data, [scan the setup QR](#alternative-load-the-setup-qr)
   when asked; the transaction stays loaded.
5. If several wallets/accounts match, choose one. Review destinations, amounts,
   fee and change, then type `SIGN`.
6. Use Sparrow's **Scan QR** to read the reply. Add any remaining signatures,
   then broadcast when complete.

An approved multisig wallet stays in RAM for the session. **Loaded wallet:**
appears on the main menu; after logout or reboot, approve the setup again.
Standard single-signature accounts do not need wallet registration.

On the reply QR, **b / Left** reopens the saved review and **Esc — Finish** returns
to the menu. Reopening does not sign again. If a camera scan fails, retry that
scan while the signed QR is still displayed.

## Transactions using more than one wallet

Thunder Den signs for **one policy per pass**, preserving existing signatures.
If a transaction spends from accounts 0 and 1, sign for account 0, return the
updated PSBT to Sparrow, then scan that updated PSBT to sign for account 1.
Each pass signs all eligible inputs for the selected wallet. There is one
transaction fee. Back/Esc returns to the chooser when several wallets matched.

## Alternative: load the setup QR

1. In Sparrow's **Settings**, display the QR beside the completed **Descriptor**.
2. On Thunder Den, choose **Scan a QR code** and scan it.
3. Review the type, threshold, public keys and paths. Thunder Den verifies its own
   key; compare the other keys with their devices or your trusted backup.
4. Compare the first receiving address with Sparrow, then type `REGISTER`.

This supports the standard sorted-multisig layouts above, not bare multisig or
MuSig2. Approving another wallet replaces the loaded one; cancelling keeps it.

## Troubleshooting

### Cannot verify transaction fee

Thunder Den refuses signing when required previous-transaction data is missing.

1. Check that Sparrow is **2.5.5 or later** and Thunder Den's keystore is
   **Airgapped Wallet (Krux)**. If it was imported as SeedSigner, configure it
   through **Krux → Scan**, checking that the public key, fingerprint and path match.
2. If other open wallet configurations contain the same keys, close their tabs
   and create a fresh transaction from the intended Krux-configured wallet.
3. You can also try **File → Show PSBT as QR…** with the transaction open. This
   preserves previous transactions already in the PSBT, even with a SeedSigner
   profile. It does **not** fetch missing ones, so it is not a replacement for the
   Krux setup.

See [fee verification](FEES.md) for the reason signing is blocked. This stricter
policy is in current source; preview 8 allowed a warning override.

### No matching signing key

Check the network, recovery words, passphrase and supplied key paths. Registering
an unrelated wallet will not fix a missing signing key.

See [validation status](STATUS.md#sparrow-integration) for automated and physical
test scope. Use fresh test words and test coins while validating your setup.
