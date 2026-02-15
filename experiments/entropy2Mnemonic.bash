#!/bin/bash
# entropy2Mnemonic.bash - Generate a BIP39 mnemonic from 128-bit (12 words) or 256-bit (24 words) hex entropy.
# Usage: ./entropy2Mnemonic.bash <hex-entropy>

set -euo pipefail

# Check that english.txt wordlist exists and appears to have 2048 lines.
if [ ! -f english.txt ]; then
    echo "Error: english.txt wordlist not found in current directory."
    exit 1
fi

wordcount=$(wc -l < english.txt)
if [ "$wordcount" -ne 2048 ]; then
    echo "Warning: english.txt does not appear to have 2048 words (found $wordcount lines)."
fi

# Check that exactly one parameter (the entropy) is provided.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <hex-entropy (128 or 256 bits)>"
    exit 1
fi

entropy="$1"

# Validate that the provided parameter is a hexadecimal string.
if ! [[ "$entropy" =~ ^[0-9a-fA-F]+$ ]]; then
    echo "Error: Entropy must be a hexadecimal string."
    exit 1
fi

# Check the length: must be 32 (128 bits) or 64 (256 bits) hex characters.
length=${#entropy}
if [ "$length" -ne 32 ] && [ "$length" -ne 64 ]; then
    echo "Error: Entropy must be 128 bits (32 hex characters) or 256 bits (64 hex characters)."
    exit 1
fi

# Determine ENT and CS based on input length.
if [ "$length" -eq 32 ]; then
    ENT=128
    CS=4
else
    ENT=256
    CS=8
fi

# Find a valid sha256 command: use sha256sum if available, otherwise try gsha256sum.
if command -v sha256sum > /dev/null; then
    sha256_cmd="sha256sum"
elif command -v gsha256sum > /dev/null; then
    sha256_cmd="gsha256sum"
else
    echo "Error: Neither sha256sum nor gsha256sum found."
    exit 1
fi

# Calculate the SHA256 hash of the raw entropy bytes.
hash_full=$(printf "%s" "$entropy" | xxd -r -p | "$sha256_cmd" | awk '{print $1}')
# For a given entropy, the checksum length in hex characters is CS/4.
hex_chars=$((CS / 4))
checksum_hex=${hash_full:0:$hex_chars}

# Append the checksum to the original entropy.
full_hex="${entropy}${checksum_hex}"

# Total bits after appending the checksum.
total_bits=$((ENT + CS))

# Convert the full hex string to binary.
# Use tr to convert to uppercase for bc, and remove newlines and backslashes (which bc may insert).
binary=$(echo "ibase=16; obase=2; $(echo "$full_hex" | tr '[:lower:]' '[:upper:]')" | bc | tr -d '\n' | tr -d '\\')

# Pad the binary string with leading zeros if necessary.
binary_len=${#binary}
if [ "$binary_len" -lt "$total_bits" ]; then
    pad_len=$((total_bits - binary_len))
    padding=$(printf "%0*d" "$pad_len" 0)
    binary="${padding}${binary}"
fi

# Split the binary string into segments of 11 bits and convert each segment to a word.
mnemonic=""
for (( i=0; i < total_bits; i+=11 )); do
    segment=${binary:$i:11}
    # Convert the 11-bit binary segment to a decimal index.
    index=$((2#$segment))
    # BIP39 wordlist is 1-indexed; add 1.
    index=$((index + 1))
    # Retrieve the word from english.txt at the corresponding line.
    word=$(sed -n "${index}p" english.txt)
    mnemonic+="${word} "
done

echo "$mnemonic"
