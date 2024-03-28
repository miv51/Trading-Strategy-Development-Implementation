
//an array class that can be interchanged with std::vector for a container with a fixed capacity (used for daily bar updates)
//i made this to replace std::vector because of the overhead that the push_back function carries.

#ifndef ARRAY_UTILS_H
#define ARRAY_UTILS_H

#include <stdexcept>

template <typename type, size_t N>
class array
{
public:
    constexpr inline size_t size() const noexcept { return length; }
    constexpr inline size_t capacity() const noexcept { return N; }

    constexpr inline void push_back(const type& object)
    {
        if (length >= N) throw std::runtime_error("Array type cannot contain more than the specified capacity.");

        elements[length] = object;

        length++;
    }

    constexpr inline void pop_back()
    {
        if (length <= 0) throw std::runtime_error("Cannot pop from an empty array.");

        length--;
    }

    constexpr inline void clear() noexcept
    {
        length = 0;
    }

    constexpr inline type& back()
    {
        if (length < 1) throw std::runtime_error("Cannot retrieve data from an empty array.");

        return elements[length - 1];
    }

    constexpr inline type& operator[](size_t index)
    {
        if (index >= length) throw std::runtime_error("Cannot retrieve data outside of the array.");

        return elements[index];
    }

private:
    size_t length = 0;

    type elements[N];
};

#endif
