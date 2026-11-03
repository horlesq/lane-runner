#pragma once
#include <array>
#include <cstddef>

// Read-only view of a contiguous sequence (pointer + count), like C++20's std::span.
// It doesn't own or copy anything: the viewed data must outlive the Span.
template <typename T>
class Span {
public:
    Span() : data_(nullptr), size_(0) {}
    Span(const T* data, std::size_t size) : data_(data), size_(size) {}

    // Implicit on purpose: any std::array of T can be passed where a Span<T> is expected.
    template <std::size_t N>
    Span(const std::array<T, N>& array) : data_(array.data()), size_(N) {}   // NOLINT(google-explicit-constructor)

    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }
    std::size_t size() const { return size_; }
    const T& operator[](std::size_t i) const { return data_[i]; }

private:
    const T* data_;
    std::size_t size_;
};
