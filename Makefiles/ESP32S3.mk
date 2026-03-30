#
# ESP32-S3 build configuration
#

ESP32S3_BUILD_DIR := $(CURDIR)/ESP32S3
ESP32S3_SDKCONFIG := $(ESP32S3_BUILD_DIR)/sdkconfig
ESP32S3_BIN := $(ESP32S3_BUILD_DIR)/DuetWiFiModule_32S3.bin
ESP32S3_ELF := $(ESP32S3_BUILD_DIR)/WiFiSocketServerRTOS.elf
ESP32S3_TARGET := esp32s3

ESP32S3:
	$(Q)if [ ! -d "$(ESP_IDF)" ]; then \
		echo "Error: ESP-IDF not found at $(ESP_IDF)"; \
		echo "Set ESP_IDF to the correct path"; \
		exit 1; \
	fi
	$(Q)echo "  BUILD   ESP32-S3"
	$(Q)bash -c 'set -e && \
		export IDF_PATH="$(ESP_IDF)" && \
		source "$$IDF_PATH/export.sh" >/dev/null 2>&1 && \
		if [ -f "$(ESP32S3_BUILD_DIR)/sdkconfig.saved" ]; then \
			cp "$(ESP32S3_BUILD_DIR)/sdkconfig.saved" sdkconfig; \
		else \
			echo "  CONFIG  ESP32-S3"; \
			idf.py -B "$(ESP32S3_BUILD_DIR)" set-target $(ESP32S3_TARGET) \
				$(if $(filter 1,$(V)),,>/dev/null 2>&1); \
		fi && \
		idf.py -B "$(ESP32S3_BUILD_DIR)" $(if $(filter 1,$(V)),-v,) build && \
		cp sdkconfig "$(ESP32S3_BUILD_DIR)/sdkconfig.saved"'
	$(Q)echo "========================================"
	$(Q)echo "ESP32-S3 firmware build complete!"
	$(Q)echo "Output: $(ESP32S3_BIN)"
	$(Q)echo "========================================"
	$(Q)if [ -f "$(ESP32S3_ELF)" ]; then \
		bash -c 'source "$(ESP_IDF)/export.sh" >/dev/null 2>&1 && \
			xtensa-esp32s3-elf-size "$(ESP32S3_ELF)"'; \
	fi

clean-ESP32S3:
	$(Q)if [ -d "$(ESP32S3_BUILD_DIR)" ]; then \
		echo "  RM      ESP32S3"; \
		rm -rf "$(ESP32S3_BUILD_DIR)"; \
	fi
