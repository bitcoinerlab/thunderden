"""Independent HD-key schema check with the standard's updated registry tags."""
import io
import json
import subprocess
import sys

sys.path.insert(0, "/opt/urtypes/src")
from urtypes import RegistryType
from urtypes.crypto import HDKey, hd_key, keypath, coin_info
from urtypes.cbor import decoder

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
    assert (key.use_info is None) == (vector["network"] == "main")
    if key.use_info is not None:
        assert key.use_info.type == 0 and key.use_info.network == 1
    assert key.to_cbor() == cbor
    descriptor = decoder.Decoder(io.BytesIO(bytes.fromhex(vector["descriptor_cbor"]))).decode()
    assert descriptor == {1: vector["descriptor"]}
    assert "/<0;1>/*" in descriptor[1] and len(descriptor[1].split("#")[1]) == 8
print("PASS: eight public hdkey and full output-descriptor exports with modern registry tags")
