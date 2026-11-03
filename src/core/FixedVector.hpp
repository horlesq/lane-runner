#pragma once
#include <array>
#include <cstddef>

// A vector with a fixed maximum size: elements live inside the object (std::array), so it never
// allocates heap memory. Typical for embedded code, e.g. a sensor's object list with room for N objects.
// T must be default-constructible (the storage is created up front).
template <typename T, std::size_t N>
class FixedVector {
public:
    // Returns false (and drops the element) when full, instead of growing like std::vector.
    bool push_back(const T& value) {
        if (size_ == N) {
            return false;
        }
        data_[size_++] = value;
        return true;
    }

    void clear() { size_ = 0; }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == N; }
    static constexpr std::size_t capacity() { return N; }

    // No bounds check, like std::vector::operator[]: the caller guarantees i < size().
    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }

    // Only the valid elements: range-for stops at size(), not at the capacity.
    T* begin() { return data_.data(); }
    T* end() { return data_.data() + size_; }
    const T* begin() const { return data_.data(); }
    const T* end() const { return data_.data() + size_; }

private:
    std::array<T, N> data_{};
    std::size_t size_ = 0;
};
