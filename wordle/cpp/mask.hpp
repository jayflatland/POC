#pragma once

#include <cassert>
#include <array>
#include <vector>

using mask_type = std::vector<bool>;

inline mask_type operator&(const mask_type& m1, const mask_type& m2)
{
    mask_type r;
    assert(m1.size() == m2.size());
    r.resize(m1.size());
    for(int i = 0; i < m1.size(); i++)
    {
        r[i] = m1[i] && m2[i];
    }
    return r;
}

// template <size_t N=65536>
// class mask_type
// {
// public:
//     bool operator[](size_t i) const
//     {
//         int byte_idx = i / 8;
//         int bit_idx = i % 8;
//         return (d_[byte_idx] & (1<<bit_idx)) != 0;
//     }

// private:
//     //max size of 
//     std::array<uint8_t, N/8> d_;
// };


mask_type full_mask(size_t n)
{
    mask_type mask;
    mask.resize(n);
    for (int i = 0; i < n; i++)
    {
        mask[i] = true;
    }
    return mask;
}
