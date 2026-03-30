#
# ESP32 build configuration
#

ESP32_BUILD_DIR := $(CURDIR)/ESP32
ESP32_SDKCONFIG := $(ESP32_BUILD_DIR)/sdkconfig
ESP32_BIN := $(ESP32_BUILD_DIR)/DuetWiFiModule_32.bin
ESP32_ELF := $(ESP32_BUILD_DIR)/WiFiSocketServerRTOS.elf
ESP32_TARGET := esp32

ESP32:
	$(Q)if [ ! -d "$(ESP_IDF)" ]; then \
		echo "Error: ESP-IDF not found at $(ESP_IDF)"; \
		echo "Set ESP_IDF to the correct path"; \
		exit 1; \
	fi
	$(Q)echo "  BUILD   ESP32"
	$(Q)bash -c 'set -e && \
		export IDF_PATH="$(ESP_IDF)" && \
		source "$$IDF_PATH/export.sh" >/dev/null 2>&1 && \
		if [ -f "$(ESP32_BUILD_DIR)/sdkconfig.saved" ]; then \
			cp "$(ESP32_BUILD_DIR)/sdkconfig.saved" sdkconfig; \
		else \
			echo "  CONFIG  ESP32"; \
			idf.py -B "$(ESP32_BUILD_DIR)" set-target $(ESP32_TARGET) \
				$(if $(filter 1,$(V)),,>/dev/null 2>&1); \
		fi && \
		idf.py -B "$(ESP32_BUILD_DIR)" $(if $(filter 1,$(V)),-v,) build && \
		cp sdkconfig "$(ESP32_BUILD_DIR)/sdkconfig.saved"'
	$(Q)echo "========================================"
	$(Q)echo "ESP32 firmware build complete!"
	$(Q)echo "Output: $(ESP32_BIN)"
	$(Q)echo "========================================"
	$(Q)if [ -f "$(ESP32_ELF)" ]; then \
		bash -c 'source "$(ESP_IDF)/export.sh" >/dev/null 2>&1 && \
			xtensa-esp32-elf-size "$(ESP32_ELF)"'; \
	fi

clean-ESP32:
	$(Q)if [ -d "$(ESP32_BUILD_DIR)" ]; then \
		echo "  RM      ESP32"; \
		rm -rf "$(ESP32_BUILD_DIR)"; \
	fi
