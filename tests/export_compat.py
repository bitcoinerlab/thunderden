"""Validate public account exports with the independent codec used by SeedSigner."""
import json
import subprocess
import sys

sys.path.insert(0, "/opt/urtypes/src")
from urtypes.crypto import Account

vectors = json.loads(subprocess.check_output([sys.argv[1], "--export-vectors"], text=True))
assert len(vectors) == 8
for vector in vectors:
    cbor = bytes.fromhex(vector["cbor"])
    account = Account.from_cbor(cbor)
    assert account.master_fingerprint.hex() == vector["fingerprint"]
    assert len(account.output_descriptors) == 1
    output = account.output_descriptors[0]
    expected = {44: ["pkh"], 49: ["sh", "wpkh"], 84: ["wpkh"], 86: ["tr"]}[vector["purpose"]]
    assert [expression.expression for expression in output.script_expressions] == expected
    key = output.hd_key()
    assert not key.master and not key.private_key
    assert len(key.key) == 33 and key.key[0] in (2, 3)
    assert "m/" + key.origin.path().replace("'", "h") == vector["path"]
    assert key.origin.source_fingerprint == account.master_fingerprint
    assert key.bip32_key() == vector["key"], "Exported metadata changed the account xpub"
    assert (key.use_info is None) == (vector["network"] == "main")
    if key.use_info is not None:
        assert key.use_info.type == 0 and key.use_info.network == 1
    assert account.to_cbor() == cbor
print("PASS: eight main/test-network BIP44/49/84/86 exports decoded and re-encoded by urtypes 1.0.1")
