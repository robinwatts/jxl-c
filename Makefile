.PHONY: parity-preflight test configure

configure:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

test: configure
	cmake --build build -j$$(nproc)
	ctest --test-dir build --output-on-failure

parity-preflight:
	bash ./preflight_parity.sh
