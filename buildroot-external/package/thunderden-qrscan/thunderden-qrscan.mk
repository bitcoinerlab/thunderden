################################################################################
#
# thunderden-qrscan
#
################################################################################

THUNDERDEN_QRSCAN_VERSION = 1.0.0
THUNDERDEN_QRSCAN_SITE = $(BR2_EXTERNAL_THUNDERDEN_PATH)/package/thunderden-qrscan/src
THUNDERDEN_QRSCAN_SITE_METHOD = local
THUNDERDEN_QRSCAN_DEPENDENCIES = zbar libv4l zlib

define THUNDERDEN_QRSCAN_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define THUNDERDEN_QRSCAN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/thunderden-qrscan $(TARGET_DIR)/usr/bin/thunderden-qrscan
endef

$(eval $(generic-package))
