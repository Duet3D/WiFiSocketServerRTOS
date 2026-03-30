#
# ESP32-C3 build configuration
#

ESP32C3_BUILD_DIR := $(CURDIR)/ESP32C3
ESP32C3_SDKCONFIG := $(ESP32C3_BUILD_DIR)/sdkconfig
ESP32C3_BIN := $(ESP32C3_BUILD_DIR)/DuetWiFiModule_32C3.bin
ESP32C3_ELF := $(ESP32C3_BUILD_DIR)/WiFiSocketServerRTOS.elf
ESP32C3_TARGET := esp32c3

ESP32C3:
	$(Q)if [ ! -d "$(ESP_IDF)" ]; then \
		echo "Error: ESP-IDF not found at $(ESP_IDF)"; \
		echo "Set ESP_IDF to the correct path"; \
		exit 1; \
	fi
	$(Q)echo "  BUILD   ESP32-C3"
	$(Q)bash -c 'set -e && \
		export IDF_PATH="$(ESP_IDF)" && \
		source "$$IDF_PATH/export.sh" >/dev/null 2>&1 && \
		if [ -f "$(ESP32C3_BUILD_DIR)/sdkconfig.saved" ]; then \
			cp "$(ESP32C3_BUILD_DIR)/sdkconfig.saved" sdkconfig; \
		else \
			echo "  CONFIG  ESP32-C3"; \
			idf.py -B "$(ESP32C3_BUILD_DIR)" set-target $(ESP32C3_TARGET) \
				$(if $(filter 1,$(V)),,>/dev/null 2>&1); \
		fi && \
		idf.py -B "$(ESP32C3_BUILD_DIR)" $(if $(filter 1,$(V)),-v,) build && \
		cp sdkconfig "$(ESP32C3_BUILD_DIR)/sdkconfig.saved"'
	$(Q)echo "========================================"
	$(Q)echo "ESP32-C3 firmware build complete!"
	$(Q)echo "Output: $(ESP32C3_BIN)"
	$(Q)echo "========================================"
	$(Q)if [ -f "$(ESP32C3_ELF)" ]; then \
		bash -c 'source "$(ESP_IDF)/export.sh" >/dev/null 2>&1 && \
			riscv32-esp-elf-size "$(ESP32C3_ELF)"'; \
	fi

clean-ESP32C3:
	$(Q)if [ -d "$(ESP32C3_BUILD_DIR)" ]; then \
		echo "  RM      ESP32C3"; \
		rm -rf "$(ESP32C3_BUILD_DIR)"; \
	fi
