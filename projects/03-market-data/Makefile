# Convenience wrapper around CMake. `make test` and `make test-sanitize` are the two entry points.
JOBS ?= $(shell nproc 2>/dev/null || echo 2)

.PHONY: all test test-sanitize bench clean

all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(JOBS)

test:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(JOBS)
	ctest --test-dir build --output-on-failure

test-sanitize:
	cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DMD_SANITIZE=ON
	cmake --build build-san -j$(JOBS)
	ctest --test-dir build-san --output-on-failure

# One command: build Release, generate/cache the benchmark feed, run it, write build/bench-results.txt.
bench:
	bash bench/run_bench.sh

clean:
	rm -rf build build-san
