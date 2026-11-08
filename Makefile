ESPHOME ?= esphome

.PHONY: sim check-version
check-version:
	@test "$$($(ESPHOME) version)" = "Version: 2026.10.2" || { echo 'Install the ESPHome version in requirements.txt'; exit 1; }

sim: check-version
	$(ESPHOME) run simulator.yaml

.PHONY: test-touch
test-touch: check-version
	SDL_VIDEODRIVER=dummy $(ESPHOME) run tests/touch.yaml
