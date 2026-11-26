#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace madcat::rhi
{
namespace detail { struct AccelerationState; struct AccelerationAccess; }
// Includes its dedicated storage, scratch and (for TLAS) instance input.
// TLAS also retains its BLAS references. Device and GPU completion rules are
// the same as BufferRef; no CPU reference count proves GPU completion.
class AccelerationRef
{
public:
    [[nodiscard]] explicit operator bool() const noexcept { return state_!=nullptr; }
private:
    friend struct detail::AccelerationAccess;
    std::shared_ptr<detail::AccelerationState> state_;
};
struct AccelerationInstance
{
    uint32_t geometry=0, customIndex=0;
    std::array<float,12> worldFromLocal{1,0,0,0,0,1,0,0,0,0,1,0}; // row-major 3x4
    friend bool operator==(const AccelerationInstance&,const AccelerationInstance&)=default;
};
}
