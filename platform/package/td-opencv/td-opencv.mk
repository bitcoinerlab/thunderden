# Fixed, source-built CPU OCR subset; source hash is checked in build/ocr-deps.sh.
TD_OPENCV_VERSION = $(OPENCV_VERSION)
TD_OPENCV_SITE = /opt/opencv
TD_OPENCV_SITE_METHOD = local
TD_OPENCV_SUPPORTS_IN_SOURCE_BUILD = NO
TD_OPENCV_INSTALL_STAGING = YES
TD_OPENCV_LICENSE = Apache-2.0
TD_OPENCV_LICENSE_FILES = LICENSE
TD_OPENCV_CONF_OPTS = -C $(BR2_EXTERNAL_THUNDERDEN_PATH)/../build/ocr-opencv.cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCPU_BASELINE=SSE2
define TD_OPENCV_TRIM_TARGET
	rm -rf $(TARGET_DIR)/usr/share/opencv4
	$(INSTALL) -D -m 0644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/thunderden-ocr/OpenCV-LICENSE
	$(INSTALL) -D -m 0644 $(@D)/3rdparty/protobuf/LICENSE $(TARGET_DIR)/usr/share/licenses/thunderden-ocr/Protobuf-LICENSE
endef
TD_OPENCV_POST_INSTALL_TARGET_HOOKS += TD_OPENCV_TRIM_TARGET
$(eval $(cmake-package))
