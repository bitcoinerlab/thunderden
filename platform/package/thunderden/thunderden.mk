THUNDERDEN_VERSION = 0.0.1
THUNDERDEN_SITE = $(BR2_EXTERNAL_THUNDERDEN_PATH)/..
THUNDERDEN_SITE_METHOD = local
THUNDERDEN_SUPPORTS_IN_SOURCE_BUILD = NO
THUNDERDEN_DEPENDENCIES = boost openssl zbar libv4l libqrencode td-opencv
THUNDERDEN_USERS = signer 1000 signer 1000 * /run /bin/false - Signer
THUNDERDEN_CONF_OPTS = -DTD_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=MinSizeRel \
	-DCORE_SOURCE_DIR=/opt/bitcoin -DUR_SOURCE_DIR=/opt/bc-ur \
	-DTD_OCR_MODEL_DIR=/usr/share/thunderden/ocr \
	-DBIP39_WORDLIST=/opt/bip39-english.txt \
	-DBIP39_WORDLIST_SHA256=$(BIP39_WORDLIST_SHA256)

define THUNDERDEN_INSTALL_OCR_MODELS
	$(INSTALL) -D -m 0644 /opt/ocr/det.onnx $(TARGET_DIR)/usr/share/thunderden/ocr/det.onnx
	$(INSTALL) -D -m 0644 /opt/ocr/rec.onnx $(TARGET_DIR)/usr/share/thunderden/ocr/rec.onnx
	$(INSTALL) -D -m 0644 /opt/ocr/characters.txt $(TARGET_DIR)/usr/share/thunderden/ocr/characters.txt
	$(INSTALL) -D -m 0644 /opt/ocr/LICENSE $(TARGET_DIR)/usr/share/licenses/thunderden-ocr/Models-LICENSE
	$(INSTALL) -D -m 0644 $(@D)/build/ocr-NOTICE.txt $(TARGET_DIR)/usr/share/licenses/thunderden-ocr/NOTICE
endef
THUNDERDEN_POST_INSTALL_TARGET_HOOKS += THUNDERDEN_INSTALL_OCR_MODELS

$(eval $(cmake-package))
