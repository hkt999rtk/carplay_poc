PYTHON ?= python3
ROOT_DIR := $(abspath ..)
BUILD_DIR := $(ROOT_DIR)/build
ARTIFACT_DIR := $(CURDIR)/artifacts
LOG_DIR := $(ARTIFACT_DIR)/logs
REPORT_DIR := $(ARTIFACT_DIR)/reports
AGGREGATED_REPORT := $(ARTIFACT_DIR)/report.json

.PHONY: test clean-artifacts

test:
	@mkdir -p "$(LOG_DIR)" "$(REPORT_DIR)"
	@$(PYTHON) scripts/run_all_tests.py \
		--root "$(ROOT_DIR)" \
		--build "$(BUILD_DIR)" \
		--report "$(AGGREGATED_REPORT)" \
		--log-dir "$(LOG_DIR)" \
		--report-dir "$(REPORT_DIR)"

clean-artifacts:
	@rm -rf "$(ARTIFACT_DIR)"
