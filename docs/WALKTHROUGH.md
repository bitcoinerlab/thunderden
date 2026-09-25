# End-to-end Thunder Den walkthrough

This guide takes you through a complete signing workflow. You will boot Thunder
Den, generate test recovery words and register a wallet policy. Then you will
check a receiving address, fund it, sign a transaction and broadcast it. The
policy uses Taproot Miniscript with a six-block relative timelock, so you can also
see why a transaction can be signed before it can be broadcast.

On the online computer, you will use the command-line client from Wizardsardine's
[async-hwi](https://github.com/wizardsardine/async-hwi). It sends requests to the
offline device through the [Thunder Den QR bridge](https://github.com/bitcoinerlab/thunderden-qr-bridge).
The bridge runs on the same online computer and opens a browser page to display
request QR codes and scan replies. You approve operations on the offline device.

A short JavaScript helper uses [BitcoinerLab's descriptors library](https://bitcoinerlab.com/modules/descriptors)
to derive addresses and build a partially signed Bitcoin transaction (PSBT).
The CLI passes this PSBT to Thunder Den for signing. For test coins, the guide
uses [Tape](https://tape.rewindbitcoin.com), Rewind Bitcoin's shared regtest
network. Its faucet sends free test coins, and its API lets you look up funding
transactions and broadcast your signed transaction. You do not need to run a
Bitcoin Core node or create a Core wallet.

## Before you start

You need Docker with Compose to build the boot image. On the online computer,
you need Node.js 22 or newer with npm, curl and a built `hwi` binary from the
[Thunder Den-enabled async-hwi branch](https://github.com/bitcoinerlab/wizardsardine-async-hwi/tree/thunderden-qr).
Run the shell commands in Bash or zsh. Disconnect other hardware wallets and
stop their simulators for this walkthrough, so the CLI selects Thunder Den.

Use **new test recovery words only**. You will generate them on the online
computer with the `bip39` library. Never use these words for real bitcoin.

## 1. Build and boot the signer

From this repository, build a fresh image. A first build can take a while:

```sh
unset DOCKER_DEFAULT_PLATFORM
docker compose run --build --name thunderden-image-build image
docker cp thunderden-image-build:/cache/out/images/thunderden.img .
docker cp thunderden-image-build:/cache/out/images/thunderden.img.sha256 .
sha256sum -c thunderden.img.sha256
docker rm thunderden-image-build
```

On macOS, use `shasum -a 256 -c thunderden.img.sha256` for the checksum step.

Follow [Build and boot](BUILD.md#2-write-it-to-a-usb-drive) to flash the **verified**
image and boot the offline device. Choose **4: Regtest** at network selection,
because that is the network type Tape uses.

## 2. Generate test words and start the bridge

On the online computer, prepare a temporary directory and install the libraries
used by the example. The last command generates a random 12-word mnemonic, which
is another name for the recovery words. Keep this terminal open for the remaining
commands so the shell variables stay available:

```sh
mkdir -p /tmp/thunderden-tape-test
cd /tmp/thunderden-tape-test
npm install --ignore-scripts bip39@3.1.0 @bitcoinerlab/descriptors@3.1.7
node --input-type=module -e 'import { generateMnemonic } from "bip39"; console.log(generateMnemonic(128))'
```

Write down the **12 words** printed by the last command. You will enter them on
Thunder Den when you make the first request. Leave the offline device at its
main menu for now. Start its scanner only when the bridge is showing a request.

In another terminal on the online computer, start the published bridge:

```sh
npx @bitcoinerlab/thunderden-qr-bridge
```

The bridge opens a browser page on the online computer. Leave the bridge running
and keep that page open for the QR exchanges. One bridge process represents one
signing-key session. Restart the bridge server before using a different mnemonic
or passphrase.

Back in the first terminal, set `HWI` to the location of **your built `hwi`
binary**:

```sh
HWI=/path/to/your/hwi
test -x "$HWI"
```

Each CLI request below, including `device list`, waits for a human-operated
QR exchange:

1. Run the CLI request first, then choose **1: Scan request or transaction** on
   Thunder Den and scan the QR shown in the bridge.
2. Review and approve on Thunder Den. Start the bridge's **response camera**
   only when Thunder Den displays its reply QR.
3. After the CLI returns, press Esc on Thunder Den to return to its menu.

## 3. Retrieve the public key and register a policy

The master fingerprint is a short label for the device's master key. The account
xpub is an extended public key that the online helper can use to derive addresses
without knowing the private keys. You will retrieve both through the CLI.

Results from this CLI go to stderr, so `2>&1` lets Bash capture them. Run
`device list` first. While it waits, choose **1** on Thunder Den, select 12 words
and enter your new mnemonic one word at a time. Leave the BIP39 passphrase empty.
The offline camera starts after recovery entry. Do not start the online response
camera until the recovery words are no longer on screen.

Listing returns the master fingerprint from the signer's reply. The xpub command
then needs a separate QR exchange and local approval. The commands below build
the public key expression automatically, with no fingerprint to copy by hand:

```sh
DEVICES=$("$HWI" --network regtest device list 2>&1)
printf '%s\n' "$DEVICES"
FP=$(printf '%s\n' "$DEVICES" | awk '$2 == "thunderden" {print $1}')
XPUB=$("$HWI" --network regtest xpub get --path "m/48h/1h/0h/2h" 2>&1)
export KEY="[$FP/48'/1'/0'/2']$XPUB"
printf 'Account key: %s\n' "$KEY"
```

Next, define the wallet's spending policy. It requires a signature from your test
key. `older(6)` also requires the funding output to reach six confirmations before
the spend can enter the mempool. The policy
uses a fixed public [NUMS internal key](../tests/qr_command_runner.cpp), a key with
no known private key. This prevents spending through the Taproot key path to
bypass the timelock. The other key, `KEY`, comes from your test mnemonic:

```sh
export NUMS=tpubD6NzVbkrYhZ4Wzt8snb1hHKjrMidYf5xQBsjMqshTmRQDhF12fEyHWCaBWXCZ3UUaaRPfbPP4AvFSQdSoqijQRsg1wb4xE2XbYGFUai3ME3
NAME='Tape timelock test'
POLICY="tr($NUMS/**,and_v(v:pk($KEY/**),older(6)))"

PROOF=$("$HWI" --network regtest wallet register --name "$NAME" --policy "$POLICY" 2>&1)
printf 'Registration proof: %s\n' "$PROOF"
```

On Thunder Den, inspect the policy and enter **REGISTER**. `PROOF` should be
64 hexadecimal characters. This is the registration HMAC returned by the signer.
The CLI sends it with later requests to show that you approved this policy.
Keep the exact `NAME`, `POLICY` and `PROOF` together. The HMAC works with that
policy and the same seed after restarting either the signer or the bridge.

## 4. Check an address and fund it

Create the helper below in your temporary directory. Its `receive` and `change`
commands derive addresses from the public keys. Later, its `psbt` command will
find a confirmed unspent output (UTXO) on Tape and prepare a transaction to spend
it. Its `finalize` command will assemble the signed transaction for broadcast.
The helper does not need your recovery words.

```sh
cat > tape.mjs <<'JS'
import { Output, Psbt, networks } from '@bitcoinerlab/descriptors';

const api = 'https://tape.rewindbitcoin.com/api';
const network = networks.regtest;
const wallet = (branch, index) => {
  const tapLeaf = `and_v(v:pk(${process.env.KEY}/${branch}/${index}),older(6))`;
  return new Output({
    descriptor: `tr(${process.env.NUMS}/${branch}/${index},${tapLeaf})`,
    network, taprootSpendPath: 'script', tapLeaf
  });
};
const receive = wallet(0, 0);
const change = wallet(1, 0);

if (process.argv[2] === 'receive') console.log(receive.getAddress());
else if (process.argv[2] === 'change') console.log(change.getAddress());
else if (process.argv[2] === 'psbt') {
  const response = await fetch(`${api}/address/${receive.getAddress()}/utxo`);
  if (!response.ok) throw new Error(`UTXO lookup: HTTP ${response.status}`);
  const utxos = await response.json();
  const utxo = utxos.find(u => u.status.confirmed && u.value > 2000);
  if (!utxo) throw new Error('Wait for a confirmed faucet UTXO');
  const previous = await fetch(`${api}/tx/${utxo.txid}/hex`);
  if (!previous.ok) throw new Error(`Previous transaction: HTTP ${previous.status}`);

  const psbt = new Psbt({ network });
  receive.updatePsbtAsInput({ psbt, vout: utxo.vout, txHex: await previous.text() });
  change.updatePsbtAsOutput({ psbt, value: BigInt(utxo.value - 1000) });
  // This preset writes hardened paths with h; the PSBT encoder expects '.
  for (const input of psbt.data.inputs)
    for (const derivation of input.tapBip32Derivation ?? [])
      derivation.path = derivation.path.replace(/(\d+)h(?=\/|$)/g, "$1'");
  if (psbt.txInputs[0].sequence !== 6) throw new Error('Missing six-block sequence');
  console.log(psbt.toBase64());
} else if (process.argv[2] === 'finalize') {
  const psbt = Psbt.fromBase64(process.env.SIGNED, { network });
  if (psbt.txInputs[0].sequence !== 6) throw new Error('Sequence changed');
  psbt.finalizeAllInputs();
  console.log(psbt.extractTransaction().toHex());
} else throw new Error('Expected receive, change, psbt or finalize');
JS

RECEIVE=$(node tape.mjs receive)
CHANGE=$(node tape.mjs change)
printf 'Receive: %s\nChange:  %s\n' "$RECEIVE" "$CHANGE"

"$HWI" --network regtest address display --index 0 \
  --wallet-name "$NAME" --wallet-policy "$POLICY" --hmac "$PROOF"
```

Compare the **address shown on Thunder Den** to `$RECEIVE` before funding it.
The CLI finishes without printing anything when you confirm the address. You can
then paste it into Tape's [faucet](https://tape.rewindbitcoin.com), or request the
test coins from the terminal:

```sh
curl -fsS -X POST --data-urlencode "address=$RECEIVE" -d 'forceConfirm=true' \
  https://tape.rewindbitcoin.com/faucet
curl -fsS "https://tape.rewindbitcoin.com/api/address/$RECEIVE/utxo"
```

The second command lists the address's unspent outputs. Wait until an output
shows `"confirmed":true` before continuing. The faucet may limit repeated
requests, and you may need to wait for Tape to mine a block.

## 5. Sign the transaction

The helper spends one funded output, sends the coins to the wallet's change
address and leaves 1,000 sats as the fee. It sets the input sequence to `6` to
satisfy the policy's relative timelock. You can sign now, even if the output has
not reached six confirmations yet.

Run the commands below and complete the signing QR exchange. On Thunder Den,
check the **1,000-sat fee**, six-block input sequence and output address against
`$CHANGE`, then enter **SIGN**. This helper does not include change-address
derivation details in the PSBT, so Thunder Den displays that output as an external
destination. Compare the full address yourself.

```sh
PSBT=$(node tape.mjs psbt)
SIGNED=$("$HWI" --network regtest psbt sign --psbt "$PSBT" \
  --wallet-name "$NAME" --wallet-policy "$POLICY" --hmac "$PROOF" 2>&1)
HEX=$(SIGNED="$SIGNED" node tape.mjs finalize)
```

`HEX` now contains the signed transaction. Finalizing the PSBT
assembles the signature and script data. It does not check whether the timelock
has matured on Tape.

## 6. Wait for the timelock and broadcast

Look up `$RECEIVE` in the [Tape explorer](https://tape.rewindbitcoin.com/explorer)
and check the funding transaction. The policy requires **at least six
confirmations** before this spend can enter the mempool. The delay starts when
the funding transaction confirms, not when you sign. Keep the same `HEX` while
you wait. You do not need another QR exchange or signature.

Once the funding transaction has six confirmations, broadcast the signed
transaction through Tape's API:

```sh
TXID=$(curl -fsS -X POST -H 'Content-Type: text/plain' \
  --data-binary "$HEX" https://tape.rewindbitcoin.com/api/tx)
printf 'Broadcast txid: %s\n' "$TXID"
curl -fsS "https://tape.rewindbitcoin.com/api/tx/$TXID/status"
```

The first command returns a transaction ID. The second checks whether it has
confirmed. You can also look up `$TXID` in the explorer. The coins are now moving
to your change address, under the same timelocked policy, minus the 1,000-sat fee.

You have tested key retrieval, policy registration, address confirmation,
offline signing and broadcast together. End the session on Thunder Den when
finished to clear the loaded keys.
