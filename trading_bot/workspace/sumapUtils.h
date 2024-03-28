
//a custom unordered map class that is optimized for retrieval and transversal speed
//i made this to replace std::unordered_map mainly because of the overhead that its iterators carry.

#ifndef SUMAP_UTILS_H
#define SUMAP_UTILS_H

#include <stdexcept>
#include <string>

template <typename dataType>
constexpr size_t hash(const dataType& key)
{
    throw std::runtime_error("Hash function for specified data type not implemented.");
}

template <>
constexpr size_t hash<std::string>(const std::string& key)
{
    size_t h = 0;

    for (const char& c : key) h = 31 * h + c;

    return h;
}

/*
unordered map that only uses stack memory and has a fixed capacity
keys must be initialized all together (cannot add keys individually using the [] operator)
N is the maximum number of key:value pairs the container can have
B is the maximum number of hash values the container can have
*/

template <typename keyDataType, typename valueDataType, size_t N, size_t B>
class staticUnorderedMap
{
public:
    staticUnorderedMap()
    {
        for (size_t& bin_size : bin_sizes) bin_size = 0;
        for (size_t& bin_index : bin_indices) bin_index = 0;
    }

    ~staticUnorderedMap() {};

    constexpr inline size_t size() const noexcept { return length; }
    constexpr inline size_t capacity() const noexcept { return N; }
    constexpr inline size_t hash_capacity() const noexcept { return B; }
    constexpr inline valueDataType* begin() noexcept { return values; }
    constexpr inline valueDataType* end() noexcept { return values + length; }

    constexpr inline valueDataType& operator[](const keyDataType& key)
    {
        size_t hash_value = hash<keyDataType>(key) % B;

        for (size_t index = 0; index < bin_sizes[hash_value]; ++index)
        {
            if (key == keys[bin_indices[hash_value] + index]) return values[bin_indices[hash_value] + index];
        }

        throw std::runtime_error("Key not found in the static unordered map.");
    }

    constexpr inline bool contains(const keyDataType& key)
    {
        size_t hash_value = hash<keyDataType>(key) % B;

        for (size_t index = 0; index < bin_sizes[hash_value]; ++index)
        {
            if (key == keys[bin_indices[hash_value] + index]) return true;
        }

        return false;
    }

    //groups the keys with the same hash values for easier access
    //takes any container that can be iterated with a range-based for loop
    inline void initializeKeys(auto& range_based_container)
    {
        length = 0;

        size_t hash_value = 0;

        for (size_t& bin_size : bin_sizes) bin_size = 0;
        for (const keyDataType& key : range_based_container) ++bin_sizes[hash<keyDataType>(key) % B]; //count the number of keys with the same hash value
        for (size_t index = 0; index < B; ++index)
        {
            //in the event no keys share a certain hash value, the keys array will not be transversed at all so there is no need to be concerned ...
            //... if multiple hash values share the same bin index

            bin_indices[index] = length;
            length += bin_sizes[index];

            if (length > N) throw std::runtime_error("Static unordered map cannot exceed its specified maximum capacity.");
        }

        for (size_t& bin_size : bin_sizes) bin_size = 0; //use bin sizes to keep track of the number of keys assigned to the keys array with the same hash value
        for (const keyDataType& key : range_based_container)
        {
            hash_value = hash<keyDataType>(key) % B;

            keys[bin_indices[hash_value] + (bin_sizes[hash_value]++)] = key;
        }
    }

private:
    size_t length = 0;

    valueDataType values[N];
    keyDataType keys[N];

    size_t bin_indices[B]; //index of key in keys is in between bin_index[hash<>(key)] and bin_index[hash<>(key)] + bin_sizes[hash<>(key)]
    size_t bin_sizes[B];
};

#endif
