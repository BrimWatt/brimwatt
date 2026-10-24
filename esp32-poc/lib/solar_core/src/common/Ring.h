#pragma once

#include <cstddef>
#include <cstdlib>

namespace solar {

/** Where big buffers come from. The board points this at PSRAM; everywhere else it is plain malloc. */
using BigAllocator = void* (*)(size_t bytes);
inline BigAllocator& bigAllocator() {
    static BigAllocator allocator = nullptr;
    return allocator;
}

/**
 * A fixed-size circular buffer for plain-old-data records: once full, a new record pushes out the oldest. The storage is
 * one block allocated on first use (from the big allocator, so on the board it lives in PSRAM and does not eat the small
 * internal heap).
 */
template <class T>
class Ring {
public:
    explicit Ring(size_t capacity) : capacity_(capacity) {}
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    ~Ring() { std::free(data_); }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    void push(const T& value) {
        if (!ensure()) return;
        data_[(head_ + size_) % capacity_] = value;
        if (size_ == capacity_) {
            head_ = (head_ + 1) % capacity_;
        } else {
            size_++;
        }
    }

    /** 0 is the oldest record. */
    const T& at(size_t index) const { return data_[(head_ + index) % capacity_]; }

private:
    bool ensure() {
        if (data_) return true;
        if (capacity_ == 0) return false;
        BigAllocator allocator = bigAllocator();
        void* block = allocator ? allocator(capacity_ * sizeof(T)) : std::malloc(capacity_ * sizeof(T));
        if (!block) {
            capacity_ = 0;   // no memory: keep nothing rather than crash
            return false;
        }
        data_ = static_cast<T*>(block);
        return true;
    }

    size_t capacity_;
    size_t size_ = 0;
    size_t head_ = 0;
    T* data_ = nullptr;
};

}  // namespace solar
