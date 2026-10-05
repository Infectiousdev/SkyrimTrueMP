#pragma once

#include "Buffer.hpp"

namespace TiltedPhoques
{
// A Buffer over memory it does not own, for reading or writing in place without a copy.
struct ViewBuffer final : Buffer
{
    ViewBuffer(uint8_t* apData, size_t aSize) noexcept
    {
        m_pData = apData;
        m_size = aSize;
    }

    ViewBuffer(const ViewBuffer&) = delete;
    ViewBuffer& operator=(const ViewBuffer&) = delete;

    // Let go of the memory so the base class does not free it.
    ~ViewBuffer() override
    {
        m_pData = nullptr;
        m_size = 0;
    }
};
} // namespace TiltedPhoques
