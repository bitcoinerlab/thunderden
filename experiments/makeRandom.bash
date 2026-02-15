#!/bin/bash

# Bitcoin Core RPC credentials
RPC_USER="bitcoinuser"
RPC_PASS="securepassword"
RPC_PORT="8332"

# Detect correct SHA-256 command (macOS vs Linux)
if command -v sha256sum &> /dev/null; then
  SHA256_CMD="sha256sum"
elif command -v gsha256sum &> /dev/null; then
  SHA256_CMD="gsha256sum"
else
  echo "❌ Error: sha256sum or gsha256sum not found! Install coreutils if on macOS."
  exit 1
fi

# Generate a unique temporary wallet name
WALLET_NAME="rewind_tmp_$(head -c 16 /dev/urandom | xxd -p)"

echo "🔹 Using wallet: $WALLET_NAME"

#  Create a descriptor wallet
curl --silent --user $RPC_USER:$RPC_PASS --data-binary \
  '{"jsonrpc":"1.0","id":"curl","method":"createwallet","params":["'"$WALLET_NAME"'", false, false, "", false, true]}' \
  -H 'content-type: text/plain;' http://127.0.0.1:$RPC_PORT/ > /dev/null

#  Extract full `listdescriptors` JSON output (including private keys)
echo "🔹 Extracting full listdescriptors output..."
DESCRIPTOR_JSON=$(curl --silent --user $RPC_USER:$RPC_PASS --data-binary \
  '{"jsonrpc":"1.0","id":"curl","method":"listdescriptors","params":[true]}' \
  -H 'content-type: text/plain;' http://127.0.0.1:$RPC_PORT/wallet/$WALLET_NAME)

if [ -z "$DESCRIPTOR_JSON" ] || [[ "$DESCRIPTOR_JSON" == "null" ]]; then
  echo "❌ Error: Failed to retrieve private descriptors!"
  exit 1
fi

echo "✅ Full Descriptors Output:"
echo "$DESCRIPTOR_JSON"

#  SHA-256 hash of full descriptor output
echo "🔹 Computing SHA-256 of descriptors..."
SHA256_HASH=$(echo -n "$DESCRIPTOR_JSON" | $SHA256_CMD | awk '{print $1}')
echo "✅ SHA-256 Hash: $SHA256_HASH"

# 5️⃣Unload the temporary wallet
echo "🔹 Unloading temporary wallet: $WALLET_NAME"
curl --silent --user $RPC_USER:$RPC_PASS --data-binary \
  '{"jsonrpc":"1.0","id":"curl","method":"unloadwallet","params":["'"$WALLET_NAME"'"]}' \
  -H 'content-type: text/plain;' http://127.0.0.1:$RPC_PORT/ > /dev/null

echo "🎉 Done! Wallet unloaded."

#Using tails Os, i can also use electrum as RNG
#Now use bitcoinerlab to generete the descritors.
#Use bip39.entroy2Mnemonic
# Then bip39.mnenomicToSeedSync
# Then create the descriptors and import them to bitcoin-core?

#MNEMONIC=$(./entropy2Mnemonic.bash $SHA256_HASH)

#now create the xprv BIP 32 root key from mnemonic
#then create a new BLANK wallet
#then call sethdseed
#THIS CANNOT WORK :( https://bitcoin.stackexchange.com/questions/89036/bip32-bitcoin-core-wallet-hdseed-format-from-xprv-master-key

#So create a blank wallet and then import descriptors?
