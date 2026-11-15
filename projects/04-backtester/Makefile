# Convenience wrapper around CMake. Python parts (integration tests, sensitivity harness) arrive in a later pass.
PY    ?= python3
BUILD ?= build
ASAN  ?= build-san
JOBS  ?= $(shell nproc 2>/dev/null || echo 2)
export PYTHONPATH := $(CURDIR)/../01-post-trade-analytics/src:$(CURDIR)/../02-risk-monitor/src:$(PYTHONPATH)

.PHONY: build test test-sanitize sensitivity clean

build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j$(JOBS) --target bt_run bt_tests mdtool

test: build
	ctest --test-dir $(BUILD) -R '^bt_' --output-on-failure

test-sanitize:
	cmake -S . -B $(ASAN) -DCMAKE_BUILD_TYPE=Debug -DBT_SANITIZE=ON
	cmake --build $(ASAN) -j$(JOBS) --target bt_run bt_tests
	ctest --test-dir $(ASAN) -R '^bt_' --output-on-failure

sensitivity: build
	@echo 'sensitivity harness is added in the next pass'

clean:
	rm -rf $(BUILD) $(ASAN) out
