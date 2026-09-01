# Thunder Den Buildroot external make include.

# Build only the Bitcoin Core binaries needed for Thunder Den runtime.
BITCOIN_CONF_OPTS += \
	-DBUILD_DAEMON=ON \
	-DBUILD_CLI=ON \
	-DBUILD_BITCOIN_BIN=OFF \
	-DBUILD_TX=OFF \
	-DBUILD_UTIL=OFF \
	-DBUILD_WALLET_TOOL=OFF \
	-DINSTALL_MAN=OFF \
	-DENABLE_EXTERNAL_SIGNER=OFF \
	-DWITH_EMBEDDED_ASMAP=OFF \
	-DREDUCE_EXPORTS=ON

include $(sort $(wildcard $(BR2_EXTERNAL_THUNDERDEN_PATH)/package/*/*.mk))
