#pragma once
#include <d3d11.h>
#include <cstring>
#include <vector>
#include <stdexcept>
namespace gpu_detail {
template <class T> class ComOwner {
    T *pointer{};

  public:
    ComOwner() = default;
    ComOwner(const ComOwner &) = delete;
    ComOwner &operator=(const ComOwner &) = delete;
    ~ComOwner() {
        if (pointer)
            pointer->Release();
    }
    T *Get() const {
        return pointer;
    }
    T **GetAddressOf() {
        return &pointer;
    }
    T *operator->() const {
        return pointer;
    }
    explicit operator bool() const {
        return pointer != nullptr;
    }
};
} // namespace gpu_detail
// One-time readback of Unity's D3D11 mesh buffers. Does not change source resources.
inline std::vector<unsigned char> readGpuBuffer(ID3D11Buffer *source) {
    if (!source)
        throw std::runtime_error("Null D3D11 mesh buffer");
    D3D11_BUFFER_DESC desc{};
    source->GetDesc(&desc);
    if (!desc.ByteWidth || desc.ByteWidth > 8 * 1024 * 1024)
        throw std::runtime_error("Mesh buffer exceeds readback limit");
    gpu_detail::ComOwner<ID3D11Device> device;
    gpu_detail::ComOwner<ID3D11DeviceContext> context;
    gpu_detail::ComOwner<ID3D11Buffer> staging;
    source->GetDevice(device.GetAddressOf());
    if (!device)
        throw std::runtime_error("Missing D3D11 device");
    device->GetImmediateContext(context.GetAddressOf());
    if (!context)
        throw std::runtime_error("Missing D3D11 context");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    HRESULT created = device->CreateBuffer(&desc, nullptr, staging.GetAddressOf());
    if (FAILED(created))
        throw std::runtime_error("D3D11 staging buffer creation failed");
    // Allocate before Map so allocation failure cannot leave a resource mapped.
    std::vector<unsigned char> data(desc.ByteWidth);
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result))
        throw std::runtime_error("D3D11 mesh readback failed");
    memcpy(data.data(), mapped.pData, data.size());
    context->Unmap(staging.Get(), 0);
    return data;
}
