# Test Thunder Den on Tape

This walkthrough tests a booted Thunder Den laptop, the QR bridge and the
`async-hwi` CLI against [Tape](https://tape.rewindbitcoin.com), a faucet-backed
regtest chain. It registers a Taproot Miniscript wallet, checks an address, signs
a transaction and broadcasts it after a six-block relative timelock. No Bitcoin
Core wallet or local node is needed: Tape supplies the funding transaction and
broadcast API.

Use a **new test mnemonic only**. The mnemonic in this guide is generated on the
online computer, so never use it for real bitcoin.

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

Follow [Build and boot](BUILD.md#2-write-it-to-a-usb-drive) to flash the **verified**
image and boot the offline laptop. Choose **4: Regtest** at network selection.

## 2. Generate test words and start the bridge

Run the following on the online computer in a new temporary directory. Keep this
terminal open for the remaining commands:

```sh
mkdir -p /tmp/thunderden-tape-test
cd /tmp/thunderden-tape-test
npm install --ignore-scripts bip39@3.1.0 @bitcoinerlab/descriptors@3.1.7
node --input-type=module -e 'import { generateMnemonic } from "bip39"; console.log(generateMnemonic(128))'
```

Write down the **12 words** printed by the last command. Do not put them in a
shell variable, repository or real wallet. Leave Thunder Den at its main menu
for now; the scanner should not start until the bridge is showing a request.

In another terminal on the online computer, start the published bridge:

```sh
npx @bitcoinerlab/thunderden-qr-bridge
```

It opens the local QR page. Leave it running. Back in the first terminal, set
`HWI` to **your installed `hwi` binary** (the CLI from the Thunder Den-enabled
`async-hwi` fork):

```sh
HWI=/path/to/your/hwi
test -x "$HWI"
"$HWI" --network regtest device list
```

`device list` reports the bridge without scanning or checking the offline
signer. Each subsequent CLI request waits for one human-operated exchange:

1. Run the CLI request first, then choose **1: Scan request or transaction** on
   Thunder Den and scan the QR shown in the bridge.
2. Review and approve on Thunder Den. Start the bridge's **response camera**
   only when Thunder Den displays its reply QR.
3. After the CLI returns, press Esc on Thunder Den to return to its menu.

## 3. Get an xpub and register a timelocked wallet

Results from this CLI go to stderr, so `2>&1` lets Bash capture them. The xpub
request asks for local approval. **Copy the fingerprint shown on Thunder Den's
public-key review screen** before dismissing it; `device list` does not know it.
Run the xpub command first; it waits while you choose **1** on Thunder Den,
select 12 words and enter your new mnemonic one word at a time. Leave the BIP39
passphrase empty. The offline camera starts after recovery entry. Do not start
the online response camera until the recovery words are no longer on screen.

```sh
XPUB=$("$HWI" --network regtest xpub get --path "m/48h/1h/0h/2h" 2>&1)
printf 'Account xpub: %s\n' "$XPUB"
FP=YOUR_EIGHT_HEX_DIGIT_FINGERPRINT
```

The policy uses the project's fixed public [NUMS internal key](../tests/qr_command_runner.cpp)
so no known private key can bypass the six-block script path. Only the `KEY`
below belongs to your test mnemonic:

```sh
export NUMS=tpubD6NzVbkrYhZ4Wzt8snb1hHKjrMidYf5xQBsjMqshTmRQDhF12fEyHWCaBWXCZ3UUaaRPfbPP4AvFSQdSoqijQRsg1wb4xE2XbYGFUai3ME3
export KEY="[$FP/48'/1'/0'/2']$XPUB"
NAME='Tape timelock test'
POLICY="tr($NUMS/**,and_v(v:pk($KEY/**),older(6)))"

PROOF=$("$HWI" --network regtest wallet register --name "$NAME" --policy "$POLICY" 2>&1)
printf 'Registration proof: %s\n' "$PROOF"
```

On Thunder Den, inspect the policy and enter **REGISTER**. `PROOF` should be
64 hexadecimal characters. Keep the exact `NAME`, `POLICY` and `PROOF` together;
the proof works with that policy and the same seed after a restart.

## 4. Check an address and fund it

Create the small online-only PSBT helper below in your temporary directory. It
derives receive and change addresses from public keys, fetches a confirmed Tape
UTXO and prepares the script-path PSBT. It does not see your mnemonic.

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

Compare the **address shown on Thunder Den** to `$RECEIVE`. A successful CLI
address confirmation has no text output. Then request test coins from Tape's
[faucet](https://tape.rewindbitcoin.com), or use its API:

```sh
curl -fsS -X POST --data-urlencode "address=$RECEIVE" -d 'forceConfirm=true' \
  https://tape.rewindbitcoin.com/faucet
curl -fsS "https://tape.rewindbitcoin.com/api/address/$RECEIVE/utxo"
```

Wait for a UTXO with `"confirmed":true`. The faucet may rate-limit requests or
require waiting for a block.

## 5. Sign and broadcast after six confirmations

```sh
PSBT=$(node tape.mjs psbt)
SIGNED=$("$HWI" --network regtest psbt sign --psbt "$PSBT" \
  --wallet-name "$NAME" --wallet-policy "$POLICY" --hmac "$PROOF" 2>&1)
HEX=$(SIGNED="$SIGNED" node tape.mjs finalize)
```

On Thunder Den, check the **1,000-sat fee**, six-block input sequence and the
output address against `$CHANGE`, then enter **SIGN**. This minimal helper does
not attach derivation hints to the output, so Thunder Den may display the
change address as an external destination: compare the full address yourself.
`finalize` proves the required signature and script data are present; it does
**not** mean the six-block timelock is mature.

Tape requires **at least six confirmations** for the funding output before this
transaction can enter its mempool. Check its funding transaction's block height
in the [explorer](https://tape.rewindbitcoin.com/explorer) and wait for six
confirmations. Do not rescan or re-sign while waiting; broadcast the same `HEX`:

```sh
TXID=$(curl -fsS -X POST -H 'Content-Type: text/plain' \
  --data-binary "$HEX" https://tape.rewindbitcoin.com/api/tx)
printf 'Broadcast txid: %s\n' "$TXID"
curl -fsS "https://tape.rewindbitcoin.com/api/tx/$TXID/status"
```

The signed transaction moves the faucet output to the wallet's change branch
minus the 1,000-sat fee. Tape's later block confirmation is visible in the
explorer. End the session on Thunder Den when finished.
