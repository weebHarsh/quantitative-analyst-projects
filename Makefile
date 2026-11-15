# Single-command entry points. Projects 3 and 4 targets exist once those projects are built.
PY ?= python3

.PHONY: help install test-all test-p1 test-p2 test-p3 test-p4 demo-p2 bench-p3 clean

help:
	@echo "make install    pip install projects 1 and 2 in editable mode"
	@echo "make test-all   run every project's tests"
	@echo "make test-p1    Project 1 (post-trade analytics)"
	@echo "make test-p2    Project 2 (risk monitor)"
	@echo "make test-p3    Project 3 (market-data normalizer), if present"
	@echo "make test-p4    Project 4 (backtester), if present"
	@echo "make demo-p2    Project 2 bad-session demo"
	@echo "make bench-p3   Project 3 benchmark, if present"

install:
	$(PY) -m pip install -q -e projects/01-post-trade-analytics --no-build-isolation
	$(PY) -m pip install -q -e projects/02-risk-monitor --no-build-isolation

test-p1:
	cd projects/01-post-trade-analytics && $(PY) -m pytest -q -p no:cacheprovider

test-p2:
	cd projects/02-risk-monitor && $(PY) -m pytest -q -p no:cacheprovider

test-p3:
	@if [ -d projects/03-market-data ]; then $(MAKE) -C projects/03-market-data test; else echo "projects/03-market-data not present"; fi

test-p4:
	@if [ -d projects/04-backtester ]; then $(MAKE) -C projects/04-backtester test; else echo "projects/04-backtester not present"; fi

test-all: test-p1 test-p2 test-p3 test-p4

demo-p2:
	cd projects/02-risk-monitor && rmon demo --scenario bad --out out/bad

bench-p3:
	@if [ -d projects/03-market-data ]; then $(MAKE) -C projects/03-market-data bench; else echo "projects/03-market-data not present"; fi

clean:
	rm -rf projects/*/build projects/*/out
