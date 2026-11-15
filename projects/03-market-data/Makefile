# Convenience wrapper around CMake. `make test` and `make test-sanitize` are the two entry points.
JOBS ?= $(shell nproc 2>/dev/null || echo 2)

.PHONY: all test test-sanitize clean

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

clean:
	rm -rf build build-san
