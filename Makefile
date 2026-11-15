ESPHOME ?= esphome
ESPHOME_VERSION := $(patsubst esphome==%,%,$(filter esphome==%,$(shell cat requirements.txt)))
GLAZE_MANAGER_DIR ?= ../glaze-manager
MANAGER_PYTHON ?= $(GLAZE_MANAGER_DIR)/.venv/bin/python

.PHONY: sim check-version
check-version:
	@test "$$($(ESPHOME) version)" = "Version: $(ESPHOME_VERSION)" || { echo 'Install the ESPHome version in requirements.txt'; exit 1; }

sim: check-version
	$(ESPHOME) compile simulator.yaml
	GLAZE_MANAGER_DIR="$(abspath $(GLAZE_MANAGER_DIR))" "$(MANAGER_PYTHON)" scripts/simulate.py

.PHONY: test-calibration test-nfc-rf test-firmware-startup test-touch
test-calibration:
	@out=$$(mktemp -d) && \
		$(CXX) -std=c++17 -Wall -Wextra -Werror tests/calibration.cpp -o "$$out/calibration" && \
		"$$out/calibration"; result=$$?; rm -rf "$$out"; exit $$result

test-nfc-rf:
	@out=$$(mktemp -d) && \
		$(CXX) -std=c++17 -Wall -Wextra -Werror tests/nfc-rf.cpp -o "$$out/nfc-rf" && \
		"$$out/nfc-rf"; result=$$?; rm -rf "$$out"; exit $$result

test-firmware-startup: check-version
	python3 tests/firmware-startup.py $(ESPHOME)

test-touch: check-version
	SDL_VIDEODRIVER=dummy $(ESPHOME) run tests/touch.yaml

.PHONY: themes previews
themes: check-version
	$(ESPHOME) run review.yaml

previews: check-version
	@mkdir -p .esphome/previews
	@out=$$(mktemp -d "$(CURDIR)/.esphome/previews/run-XXXXXX") && \
		SDL_VIDEODRIVER=dummy ESPHOME_SNAPSHOT_DIR="$$out" GLAZE_EXPORT_PREVIEWS=1 $(ESPHOME) run review.yaml && \
		printf 'Captured native-resolution screens in %s\n' "$$out"

.PHONY: bench
bench: check-version
	$(ESPHOME) compile bench.yaml

.PHONY: scaffold test-studio
scaffold: check-version
	$(ESPHOME) run scaffold.yaml

test-studio: check-version
	SDL_VIDEODRIVER=dummy $(ESPHOME) run tests/nfc.yaml
	$(ESPHOME) compile tests/studio.yaml
	GLAZE_MANAGER_DIR="$(abspath $(GLAZE_MANAGER_DIR))" "$(MANAGER_PYTHON)" scripts/simulate.py --test
	GLAZE_MANAGER_DIR="$(abspath $(GLAZE_MANAGER_DIR))" "$(MANAGER_PYTHON)" scripts/simulate.py --test-network

.PHONY: firmware check-firmware
firmware: check-version
	$(ESPHOME) compile scale.yaml

check-firmware: check-version
	$(ESPHOME) compile tests/firmware.yaml
