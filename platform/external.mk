include $(sort $(wildcard $(BR2_EXTERNAL_THUNDERDEN_PATH)/package/*/*.mk))

# Decode only QR images in ZBar; the application uses libv4l directly for capture.
ZBAR_CONF_OPTS += --enable-codes=qrcode --disable-video --disable-nls --without-jpeg
LIBV4L_CONF_OPTS += -Dv4l-plugins=false -Dv4l-wrappers=false -Dgconv=disabled
