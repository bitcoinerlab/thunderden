#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

from embit import psbt


def read_signed_psbt_text(args: argparse.Namespace) -> str:
    if args.signed_psbt:
        return args.signed_psbt.strip()
    if args.signed_psbt_file:
        return Path(args.signed_psbt_file).read_text(encoding="utf-8").strip()
    raise ValueError("Provide --signed-psbt or --signed-psbt-file")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify signed PSBT against a Thunder Den test vector.")
    parser.add_argument("--vector", required=True, help="Vector id, e.g. tv1-testnet-basic")
    parser.add_argument("--signed-psbt", help="Signed PSBT base64 string")
    parser.add_argument("--signed-psbt-file", help="Path to file containing signed PSBT base64")
    args = parser.parse_args()

    base_dir = Path(__file__).resolve().parent
    vector_dir = base_dir / "vectors" / args.vector
    metadata_path = vector_dir / "metadata.json"
    unsigned_path = vector_dir / "unsigned.psbt.txt"

    if not metadata_path.exists():
        raise SystemExit(f"Vector metadata not found: {metadata_path}")
    if not unsigned_path.exists():
        raise SystemExit(f"Vector unsigned PSBT not found: {unsigned_path}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    unsigned_b64 = unsigned_path.read_text(encoding="ascii").strip()
    signed_b64 = read_signed_psbt_text(args)

    if not signed_b64.startswith("cHNidP"):
        raise SystemExit("Signed PSBT does not look like base64 PSBT (missing cHNidP prefix)")

    unsigned_psbt = psbt.PSBT.from_base64(unsigned_b64)
    signed_psbt = psbt.PSBT.from_base64(signed_b64)

    unsigned_tx_hex = unsigned_psbt.tx.serialize().hex()
    signed_tx_hex = signed_psbt.tx.serialize().hex()
    if signed_tx_hex != unsigned_tx_hex:
        raise SystemExit("Signed PSBT unsigned-transaction section differs from vector unsigned PSBT")

    txid = signed_psbt.tx.txid().hex()
    expected_txid = metadata["unsigned_txid"]
    if txid != expected_txid:
        raise SystemExit(f"Txid mismatch: expected {expected_txid}, got {txid}")

    expected_inputs = int(metadata["input_count"])
    expected_outputs = int(metadata["output_count"])
    if len(signed_psbt.inputs) != expected_inputs:
        raise SystemExit(f"Input count mismatch: expected {expected_inputs}, got {len(signed_psbt.inputs)}")
    if len(signed_psbt.outputs) != expected_outputs:
        raise SystemExit(f"Output count mismatch: expected {expected_outputs}, got {len(signed_psbt.outputs)}")

    for idx, inp in enumerate(signed_psbt.inputs):
        witness = inp.final_scriptwitness
        has_final_witness = witness is not None and len(getattr(witness, "items", [])) > 0
        has_partial = len(inp.partial_sigs) > 0
        if not (has_final_witness or has_partial):
            raise SystemExit(f"Input {idx} has no final witness and no partial signatures")

    print(f"Vector: {args.vector}")
    print(f"Network: {metadata['network']}")
    print(f"Unsigned txid: {txid}")
    print("Verification: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
