#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from embit import bip32, bip39, psbt, script, transaction
from embit.psbt import DerivationPath


@dataclass(frozen=True)
class InputSpec:
    txid_hex: str
    vout: int
    amount_sat: int
    path: str


@dataclass(frozen=True)
class OutputSpec:
    amount_sat: int
    path: str | None = None
    address: str | None = None


@dataclass(frozen=True)
class VectorSpec:
    vector_id: str
    title: str
    description: str
    network: str
    mnemonic: str
    passphrase: str
    inputs: tuple[InputSpec, ...]
    outputs: tuple[OutputSpec, ...]


MNEMONIC_PUBLIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"


VECTOR_SPECS: tuple[VectorSpec, ...] = (
    VectorSpec(
        vector_id="tv1-testnet-basic",
        title="Testnet basic (1 input)",
        description="Single-input p2wpkh testnet signing baseline.",
        network="testnet",
        mnemonic=MNEMONIC_PUBLIC,
        passphrase="",
        inputs=(
            InputSpec(
                txid_hex="11" * 32,
                vout=0,
                amount_sat=150000,
                path="m/84h/1h/0h/0/0",
            ),
        ),
        outputs=(
            OutputSpec(
                amount_sat=149000,
                path="m/84h/1h/0h/1/0",
            ),
        ),
    ),
    VectorSpec(
        vector_id="tv2-testnet-two-inputs",
        title="Testnet multi-input (2 inputs)",
        description="Two-input p2wpkh testnet signing path coverage.",
        network="testnet",
        mnemonic=MNEMONIC_PUBLIC,
        passphrase="",
        inputs=(
            InputSpec(
                txid_hex="22" * 32,
                vout=1,
                amount_sat=80000,
                path="m/84h/1h/0h/0/0",
            ),
            InputSpec(
                txid_hex="33" * 32,
                vout=0,
                amount_sat=50000,
                path="m/84h/1h/0h/0/1",
            ),
        ),
        outputs=(
            OutputSpec(
                amount_sat=128500,
                path="m/84h/1h/0h/1/1",
            ),
        ),
    ),
    VectorSpec(
        vector_id="tv3-testnet-passphrase",
        title="Testnet with BIP39 passphrase",
        description="Single-input signing with non-empty passphrase.",
        network="testnet",
        mnemonic=MNEMONIC_PUBLIC,
        passphrase="TREZOR",
        inputs=(
            InputSpec(
                txid_hex="44" * 32,
                vout=2,
                amount_sat=210000,
                path="m/84h/1h/0h/0/0",
            ),
        ),
        outputs=(
            OutputSpec(
                amount_sat=208000,
                path="m/84h/1h/0h/1/0",
            ),
        ),
    ),
    VectorSpec(
        vector_id="tv4-mainnet-basic",
        title="Mainnet basic (1 input)",
        description="Mainnet-mode signing baseline for network selector checks.",
        network="main",
        mnemonic=MNEMONIC_PUBLIC,
        passphrase="",
        inputs=(
            InputSpec(
                txid_hex="55" * 32,
                vout=0,
                amount_sat=90000,
                path="m/84h/0h/0h/0/0",
            ),
        ),
        outputs=(
            OutputSpec(
                amount_sat=88000,
                path="m/84h/0h/0h/1/0",
            ),
        ),
    ),
)


def network_key(network: str) -> str:
    return "main" if network == "main" else "test"


def coin_type(network: str) -> int:
    return 0 if network == "main" else 1


def derive_root(mnemonic: str, passphrase: str, network: str):
    seed = bip39.mnemonic_to_seed(mnemonic, passphrase)
    return bip32.HDKey.from_seed(seed, version=bip32.NETWORKS[network_key(network)]["xprv"])


def p2wpkh_for_path(root: bip32.HDKey, path: str, network: str):
    child = root.derive(path)
    pub = child.key.get_public_key()  # type: ignore[attr-defined]
    spk = script.p2wpkh(pub)
    return child, pub, spk, spk.address(network=bip32.NETWORKS[network_key(network)])


def build_unsigned_psbt(spec: VectorSpec):
    root = derive_root(spec.mnemonic, spec.passphrase, spec.network)
    account_path = f"m/84h/{coin_type(spec.network)}h/0h"
    account_xprv = root.derive(account_path).to_base58()

    vin = [
        transaction.TransactionInput(txid=bytes.fromhex(inp.txid_hex), vout=inp.vout)
        for inp in spec.inputs
    ]

    output_details = []
    tx_outputs = []
    for out in spec.outputs:
        if out.path:
            _, _, spk, addr = p2wpkh_for_path(root, out.path, spec.network)
            tx_outputs.append(transaction.TransactionOutput(value=out.amount_sat, script_pubkey=spk))
            output_details.append(
                {
                    "amount_sat": out.amount_sat,
                    "path": out.path,
                    "address": addr,
                    "script_pubkey_hex": spk.data.hex(),
                }
            )
        else:
            assert out.address is not None
            spk = script.address_to_scriptpubkey(out.address)
            assert spk is not None
            tx_outputs.append(transaction.TransactionOutput(value=out.amount_sat, script_pubkey=spk))
            output_details.append(
                {
                    "amount_sat": out.amount_sat,
                    "path": None,
                    "address": out.address,
                    "script_pubkey_hex": spk.data.hex(),
                }
            )

    tx_obj = transaction.Transaction(version=2, vin=vin, vout=tx_outputs, locktime=0)
    p = psbt.PSBT(tx_obj)

    input_details = []
    for idx, inp in enumerate(spec.inputs):
        _, pub, spk, addr = p2wpkh_for_path(root, inp.path, spec.network)
        p.inputs[idx].witness_utxo = transaction.TransactionOutput(value=inp.amount_sat, script_pubkey=spk)
        p.inputs[idx].bip32_derivations[pub] = DerivationPath(
            root.my_fingerprint,
            bip32.parse_path(inp.path),
        )
        input_details.append(
            {
                "txid_hex": inp.txid_hex,
                "vout": inp.vout,
                "amount_sat": inp.amount_sat,
                "path": inp.path,
                "address": addr,
                "script_pubkey_hex": spk.data.hex(),
            }
        )

    for idx, out in enumerate(spec.outputs):
        if not out.path:
            continue
        _, pub, _, _ = p2wpkh_for_path(root, out.path, spec.network)
        p.outputs[idx].bip32_derivations[pub] = DerivationPath(
            root.my_fingerprint,
            bip32.parse_path(out.path),
        )

    unsigned_b64 = p.to_base64()
    metadata = {
        "id": spec.vector_id,
        "title": spec.title,
        "description": spec.description,
        "network": spec.network,
        "mnemonic": spec.mnemonic,
        "passphrase": spec.passphrase,
        "account_path": account_path,
        "account_xprv": account_xprv,
        "master_fingerprint_hex": root.my_fingerprint.hex(),
        "unsigned_txid": tx_obj.txid().hex(),
        "unsigned_psbt_sha256": hashlib.sha256(unsigned_b64.encode("ascii")).hexdigest(),
        "input_count": len(spec.inputs),
        "output_count": len(spec.outputs),
        "inputs": input_details,
        "outputs": output_details,
        "notes": [
            "Previous outputs are synthetic placeholders for offline signing tests.",
            "These vectors are not intended for broadcast to Bitcoin network.",
        ],
    }
    return unsigned_b64, metadata


def write_vector_files(base_dir: Path, spec: VectorSpec, unsigned_b64: str, metadata: dict, no_qr: bool):
    vector_dir = base_dir / "vectors" / spec.vector_id
    vector_dir.mkdir(parents=True, exist_ok=True)

    psbt_file = vector_dir / "unsigned.psbt.txt"
    psbt_file.write_text(unsigned_b64 + "\n", encoding="ascii")

    metadata_file = vector_dir / "metadata.json"
    metadata_file.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    qr_file = vector_dir / "unsigned.psbt.qr.png"
    if not no_qr:
        subprocess.run(
            [
                "qrencode",
                "-o",
                str(qr_file),
                "-l",
                "M",
                "-m",
                "2",
                "-s",
                "8",
                unsigned_b64,
            ],
            check=True,
        )

    return {
        "id": spec.vector_id,
        "title": spec.title,
        "network": spec.network,
        "unsigned_psbt_file": str(psbt_file.relative_to(base_dir)),
        "metadata_file": str(metadata_file.relative_to(base_dir)),
        "qr_png_file": str(qr_file.relative_to(base_dir)) if not no_qr else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Thunder Den QR signing test vectors.")
    parser.add_argument(
        "--no-qr",
        action="store_true",
        help="Skip QR image generation (unsigned PSBT text + metadata only).",
    )
    args = parser.parse_args()

    base_dir = Path(__file__).resolve().parent
    vectors_dir = base_dir / "vectors"
    vectors_dir.mkdir(parents=True, exist_ok=True)

    index = {
        "description": "Thunder Den deterministic QR-signing vectors.",
        "vectors": [],
    }

    for spec in VECTOR_SPECS:
        unsigned_b64, metadata = build_unsigned_psbt(spec)
        entry = write_vector_files(base_dir, spec, unsigned_b64, metadata, args.no_qr)
        index["vectors"].append(entry)

    (base_dir / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(VECTOR_SPECS)} vectors to {vectors_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
