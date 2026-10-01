"""Independent public HD-key, text and standard account QR checks."""
import json
import subprocess
import sys

sys.path.insert(0, "/opt/urtypes/src")
from urtypes import RegistryType
from urtypes.crypto import Account, HDKey, hd_key, keypath, coin_info

# HD-key v2 changes only these registry tags, not the key schema.
hd_key.CRYPTO_HDKEY = RegistryType("hdkey", 40303)
keypath.CRYPTO_KEYPATH = RegistryType("keypath", 40304)
coin_info.CRYPTO_COIN_INFO = RegistryType("coin-info", 40305)

vectors = json.loads(subprocess.check_output([sys.argv[1], "--export-vectors"], text=True))
assert len(vectors) == 8
for vector in vectors:
    cbor = bytes.fromhex(vector["cbor"])
    key = HDKey.from_cbor(cbor)
    assert not key.master and not key.private_key
    assert len(key.key) == 33 and key.key[0] in (2, 3)
    assert "m/" + key.origin.path().replace("'", "h") == vector["path"]
    assert key.origin.source_fingerprint.hex() == vector["fingerprint"]
    assert key.bip32_key() == vector["key"], "Exported metadata changed the account xpub"
    assert vector["public_key_text"] == f'[{vector["fingerprint"]}{vector["path"][1:]}]{key.bip32_key()}'
    assert (key.use_info is None) == (vector["network"] == "main")
    if key.use_info is not None:
        assert key.use_info.type == 0 and key.use_info.network == 1
    assert key.to_cbor() == cbor
print("PASS: eight public-key text/HD-key encodings retain the reviewed keys and origins")

# crypto-account deliberately uses the deployed legacy registry used by
# SeedSigner. Check the complete account wrapper with an independent codec.
hd_key.CRYPTO_HDKEY = RegistryType("crypto-hdkey", 303)
keypath.CRYPTO_KEYPATH = RegistryType("crypto-keypath", 304)
coin_info.CRYPTO_COIN_INFO = RegistryType("crypto-coin-info", 305)
for vector in vectors:
    cbor = bytes.fromhex(vector["account_cbor"])
    account = Account.from_cbor(cbor)
    assert account.master_fingerprint.hex() == vector["fingerprint"]
    assert len(account.output_descriptors) == 1
    output = account.output_descriptors[0]
    assert [expr.tag for expr in output.script_expressions] == {44: [403], 49: [400, 404], 84: [404], 86: [409]}[vector["purpose"]]
    assert output.crypto_key.bip32_key() == vector["key"]
    assert output.crypto_key.origin.path().replace("'", "h") == vector["path"][2:]
    assert account.to_cbor() == cbor
wallets = json.loads(subprocess.check_output([sys.argv[1], "--wallet-vectors"], text=True))
for wallet in wallets:
    for expected, exported in zip(wallet["keys"], wallet["accounts"], strict=True):
        cbor = bytes.fromhex(exported["cbor"])
        account = Account.from_cbor(cbor)
        assert account.master_fingerprint.hex() == expected[1:9]
        assert len(account.output_descriptors) == 1
        output = account.output_descriptors[0]
        assert [expr.tag for expr in output.script_expressions] == [[400], [400, 401], [401]][wallet["kind"]]
        key = output.crypto_key
        assert not key.private_key and not key.master and key.children is None
        origin = key.origin.source_fingerprint.hex() + "/" + key.origin.path().replace("'", "h")
        assert f"[{origin}]{key.bip32_key()}" == expected
        assert (key.use_info is None) == (wallet["network"] == "main")
        if key.use_info is not None:
            assert key.use_info.type == 0 and key.use_info.network == 1
        assert account.to_cbor() == cbor
        assert exported["ur"].startswith("UR:CRYPTO-ACCOUNT/")
print("PASS: standard multisig account QRs retain script type, network, xpub and full origin")
