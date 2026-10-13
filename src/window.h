#pragma once

#include <cstdint>
#include <vector>

namespace sp {

// A fixed-size block of consecutive samples. Moved, never copied, through the queue.
struct Window {
    std::uint64_t index = 0;
    std::vector<float> samples;
};

}  // namespace sp
