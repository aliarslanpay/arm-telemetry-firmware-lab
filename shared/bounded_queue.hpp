// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
namespace telemetry {
// External synchronization is required if used across execution contexts.
template<class T, size_t Capacity> struct BoundedQueue {
    static_assert(Capacity > 0, "Queue must have storage");
    T entries[Capacity];
    size_t read, count;
    bool push(const T &value) {
        if (count == Capacity) return false; // Drop newest; preserve accepted FIFO order.
        entries[(read + count) % Capacity] = value; ++count; return true;
    }
    bool pop(T &value) {
        if (!count) return false;
        value = entries[read]; read = (read + 1) % Capacity; --count; return true;
    }
    void clear() { read = count = 0; }
};
} // namespace telemetry
