/* wiz3D - ID3D10Buffer proxy implementation (Option B for DX10) */

#include "StdAfx.h"
#include "Buffer10Proxy.h"
#include "Device10Proxy.h"
#include "proxy_factory.h"
#include "../S3DAPI/GlobalData.h"
#include <vector>

#pragma comment(lib, "dxguid.lib")

namespace wiz3d
{

// 4c10: same projection-matrix-shaped 4x4 float scan as Context11Proxy's
// ApplyEyeShiftToCB. Duplicated here rather than shared because the DX11
// version is static inside Context11Proxy.cpp; keeping it local keeps the
// DX10 path independent of the DX11 .obj.
static void ApplyEyeShiftToCB10(unsigned char* data, size_t byteCount, float eyeShift)
{
    if (eyeShift == 0.f) return;
    constexpr size_t kMat4Bytes = 16 * sizeof(float);
    if (byteCount < kMat4Bytes) return;
    for (size_t off = 0; off + kMat4Bytes <= byteCount; off += 4)
    {
        float* f = reinterpret_cast<float*>(data + off);
        if (f[11] != 1.f) continue;
        if (f[15] != 0.f) continue;
        if (f[0]  == 0.f) continue;
        if (f[5]  == 0.f) continue;
        f[8] += eyeShift * f[0];
    }
}

Buffer10Proxy::Buffer10Proxy(ID3D10Buffer* real, Device10Proxy* parent)
    : m_real(real)
    , m_parent(parent)
    , m_refs(1)
    , m_vsBound(false)
    , m_activeMapValid(false)
    , m_activeMapData(nullptr)
    , m_activeMapByteWidth(0)
    , m_activeMapType(D3D10_MAP_WRITE_DISCARD)
{
}

Buffer10Proxy::~Buffer10Proxy()
{
    if (m_real) { m_real->Release(); m_real = nullptr; }
}

ULONG STDMETHODCALLTYPE Buffer10Proxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE Buffer10Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown        ||
        riid == IID_ID3D10DeviceChild ||
        riid == IID_ID3D10Resource  ||
        riid == IID_ID3D10Buffer)
    {
        *ppvObj = static_cast<ID3D10Buffer*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_wiz3D_Buffer10Proxy)
    {
        *ppvObj = static_cast<IUnknown*>(static_cast<ID3D10Buffer*>(this));
        AddRef();
        return S_OK;
    }
    return m_real->QueryInterface(riid, ppvObj);
}

void STDMETHODCALLTYPE Buffer10Proxy::GetDevice(ID3D10Device** ppDevice)
{
    if (!ppDevice) return;
    if (m_parent)
    {
        *ppDevice = static_cast<ID3D10Device*>(m_parent);
        m_parent->AddRef();
        return;
    }
    m_real->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE Buffer10Proxy::Map(D3D10_MAP MapType, UINT MapFlags, void** ppData)
{
    HRESULT hr = m_real->Map(MapType, MapFlags, ppData);
    if (FAILED(hr) || !ppData || !*ppData) return hr;

    m_activeMapValid = false;
    if (!gInfo.UseCOMWrapReplay) return hr;
    if (!m_parent || !m_parent->IsPresentHookActive()) return hr;
    if (!m_vsBound) return hr;

    if (MapType != D3D10_MAP_WRITE_DISCARD &&
        MapType != D3D10_MAP_WRITE &&
        MapType != D3D10_MAP_WRITE_NO_OVERWRITE &&
        MapType != D3D10_MAP_READ_WRITE)
        return hr;

    D3D10_BUFFER_DESC desc;
    m_real->GetDesc(&desc);
    if ((desc.BindFlags & D3D10_BIND_CONSTANT_BUFFER) == 0) return hr;
    if (desc.ByteWidth == 0) return hr;

    m_activeMapValid     = true;
    m_activeMapData      = *ppData;
    m_activeMapByteWidth = desc.ByteWidth;
    m_activeMapType      = MapType;
    return hr;
}

void STDMETHODCALLTYPE Buffer10Proxy::Unmap()
{
    if (m_activeMapValid && m_activeMapData && m_activeMapByteWidth && m_parent)
    {
        std::vector<unsigned char> bytes(m_activeMapByteWidth);
        memcpy(bytes.data(), m_activeMapData, m_activeMapByteWidth);
        D3D10_MAP mt = m_activeMapType;
        // Keep this proxy alive across the frame; the closure holds a ref.
        AddRef();
        Buffer10Proxy* self = this;
        m_parent->PushFrameCommand(
            [self, mt, bytes]() {
                if (self->m_parent &&
                    self->m_parent->ActiveEye() == Device10Proxy::Eye::Right)
                {
                    void* mapped = nullptr;
                    if (SUCCEEDED(self->m_real->Map(mt, 0, &mapped)) && mapped)
                    {
                        memcpy(mapped, bytes.data(), bytes.size());
                        ApplyEyeShiftToCB10(static_cast<unsigned char*>(mapped),
                                            bytes.size(), gInfo.COMWrapEyeShift);
                        self->m_real->Unmap();
                    }
                }
                self->Release();
            });
    }
    m_activeMapValid = false;
    m_real->Unmap();
}

} // namespace wiz3d
