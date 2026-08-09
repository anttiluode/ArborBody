#!/usr/bin/env bash
# Builds and runs the core tests and the demo renderer. No JUCE, no CMake.
set -e
cd "$(dirname "$0")/.."
mkdir -p build_core demos

echo "building tests..."
g++ -O2 -std=c++17 -o build_core/core_test \
    Tests/core_test.cpp Core/ArborGraph.cpp Core/Mesh.cpp Core/Voice.cpp Core/Instrument.cpp -lm

echo "building demo renderer..."
g++ -O2 -std=c++17 -o build_core/render_demo \
    Tests/render_demo.cpp Core/ArborGraph.cpp Core/Mesh.cpp Core/Voice.cpp Core/Instrument.cpp -lm

./build_core/core_test
./build_core/render_demo demos
echo "demos are in ./demos"
