#pragma once
// A DemonicEngine with its long-buffer memory, for the host tests. Together they are about 1 MB, so
// tests create them on the heap (std::make_unique<TestEngine>(fs)) rather than on the stack.
#include "demonic_engine.hpp"

struct TestEngine {
    DemonicEngineMemory mem;
    DemonicEngine engine;
    explicit TestEngine(double fs) : engine(static_cast<float>(fs), mem) {}
};
