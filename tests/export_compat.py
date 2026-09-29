"""Independent public HD-key and compact output-descriptor schema checks."""
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
    assert set(descriptor) == {1, 2} and len(descriptor[2]) == 1
    assert descriptor[2][0].tag == 40303
    nested = HDKey.from_data_item(descriptor[2][0])
    assert nested.to_cbor() == cbor, "Descriptor key differs from the standalone public export"
    source = descriptor[1]
    assert source.count("@0") == 1 and "/<0;1>/*" in source and "#" not in source
    origin = nested.origin.source_fingerprint.hex() + "/" + nested.origin.path().replace("'", "h")
    expanded = source.replace("@0", f"[{origin}]{nested.bip32_key()}")
    assert expanded == vector["descriptor"].split("#")[0], "Compact export changed the reviewed descriptor"
    assert len(vector["descriptor"].split("#")[1]) == 8
print("PASS: eight public hdkey and compact output-descriptor exports reconstruct the reviewed descriptors")
