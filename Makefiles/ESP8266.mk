#
# ESP8266 build configuration
#

ESP8266_BUILD_DIR := $(CURDIR)/ESP8266
ESP8266_SDKCONFIG := $(ESP8266_BUILD_DIR)/sdkconfig
ESP8266_BIN := $(ESP8266_BUILD_DIR)/DuetWiFiServer.bin

ESP8266_MAKE_ARGS := -f $(CURDIR)/Makefile.esp8266 \
	BUILD_DIR_BASE="$(ESP8266_BUILD_DIR)" \
	SDKCONFIG="$(ESP8266_SDKCONFIG)" \
	BATCH_BUILD=1

ESP8266:
	$(Q)if [ ! -d "$(ESP8266_SDK)" ]; then \
		echo "Error: ESP8266 RTOS SDK not found at $(ESP8266_SDK)"; \
		echo "Set ESP8266_SDK to the correct path"; \
		exit 1; \
	fi
	$(Q)echo "  BUILD   ESP8266"
	$(Q)mkdir -p "$(ESP8266_BUILD_DIR)"
	$(Q)bash -c 'set -e && \
		export IDF_PATH="$(ESP8266_SDK)" && \
		source "$$IDF_PATH/export.sh" >/dev/null 2>&1 && \
		if [ ! -f "$(ESP8266_SDKCONFIG)" ]; then \
			echo "  CONFIG  ESP8266"; \
			$(MAKE) $(ESP8266_MAKE_ARGS) defconfig >/dev/null 2>&1; \
		fi && \
		$(MAKE) $(ESP8266_MAKE_ARGS) -j$(NPROC)'
	$(Q)echo "========================================"
	$(Q)echo "ESP8266 firmware build complete!"
	$(Q)echo "Output: $(ESP8266_BIN)"
	$(Q)echo "========================================"

clean-ESP8266:
	$(Q)if [ -d "$(ESP8266_BUILD_DIR)" ]; then \
		echo "  RM      ESP8266"; \
		rm -rf "$(ESP8266_BUILD_DIR)"; \
	fi
