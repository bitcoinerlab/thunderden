include $(sort $(wildcard $(BR2_EXTERNAL_THUNDERDEN_PATH)/package/*/*.mk))

# Decode only QR images in ZBar; the application uses libv4l directly for capture.
ZBAR_CONF_OPTS += --enable-codes=qrcode --disable-video --disable-nls --without-jpeg
LIBV4L_CONF_OPTS += -Dv4l-plugins=false -Dv4l-wrappers=false -Dgconv=disabled

# Keep only the init/launch helpers. No setuid executable is needed.
BUSYBOX_PERMISSIONS = /bin/busybox f 0755 0 0 - - - - -
define THUNDERDEN_MINIMAL_BUSYBOX
	$(BUSYBOX_MAKE_ENV) $(MAKE) $(BUSYBOX_MAKE_OPTS) -C $(@D) allnoconfig
	support/kconfig/merge_config.sh -m -O $(@D) $(@D)/.config $(BR2_EXTERNAL_THUNDERDEN_PATH)/busybox.config
	yes "" | $(BUSYBOX_MAKE_ENV) $(MAKE) $(BUSYBOX_MAKE_OPTS) -C $(@D) oldconfig
endef
BUSYBOX_PRE_CONFIGURE_HOOKS += THUNDERDEN_MINIMAL_BUSYBOX
