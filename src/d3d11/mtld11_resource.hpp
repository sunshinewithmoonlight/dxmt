#pragma once
#include "ftl.hpp"
#include "Metal/MTLBuffer.hpp"
#include "Metal/MTLCommandBuffer.hpp"
#include "Metal/MTLTexture.hpp"
#include "d3d11_1.h"
#include "d3d11_device_child.hpp"
#include "com/com_pointer.hpp"
#include "com/com_guid.hpp"
#include "d3d11_view.hpp"
#include "dxgi_resource.hpp"
#include "dxmt_binding.hpp"
#include "dxmt_resource_binding.hpp"
#include "log/log.hpp"
#include <memory>
#include <type_traits>

typedef struct MappedResource {
  void *pData;
  uint32_t RowPitch;
  uint32_t DepthPitch;
} MappedResource;

DEFINE_COM_INTERFACE("d8a49d20-9a1f-4bb8-9ee6-442e064dce23", IDXMTResource)
    : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
      const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc,
      ID3D11ShaderResourceView1 **ppView) = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
      const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc,
      ID3D11UnorderedAccessView1 **ppView) = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateRenderTargetView(
      const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc,
      ID3D11RenderTargetView1 **ppView) = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilView(
      const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
      ID3D11DepthStencilView **ppView) = 0;
};

struct MTL_STAGING_RESOURCE {
  MTL::Buffer *Buffer;
};

enum MTL_BINDABLE_RESIDENCY_MASK : uint32_t {
  MTL_RESIDENCY_NULL = 0,
  MTL_RESIDENCY_VERTEX_READ = 1 << 0,
  MTL_RESIDENCY_VERTEX_WRITE = 1 << 1,
  MTL_RESIDENCY_FRAGMENT_READ = 1 << 2,
  MTL_RESIDENCY_FRAGMENT_WRITE = 1 << 3,
  MTL_RESIDENCY_OBJECT_READ = 1 << 4,
  MTL_RESIDENCY_OBJECT_WRITE = 1 << 5,
  MTL_RESIDENCY_MESH_READ = 1 << 6,
  MTL_RESIDENCY_MESH_WRITE = 1 << 7,
  MTL_RESIDENCY_READ = MTL_RESIDENCY_VERTEX_READ | MTL_RESIDENCY_FRAGMENT_READ |
                       MTL_RESIDENCY_OBJECT_READ | MTL_RESIDENCY_MESH_READ,
  MTL_RESIDENCY_WRITE = MTL_RESIDENCY_VERTEX_WRITE |
                        MTL_RESIDENCY_FRAGMENT_WRITE |
                        MTL_RESIDENCY_OBJECT_WRITE | MTL_RESIDENCY_MESH_WRITE,
};

struct SIMPLE_RESIDENCY_TRACKER {
  uint64_t last_encoder_id = 0;
  MTL_BINDABLE_RESIDENCY_MASK last_residency_mask = MTL_RESIDENCY_NULL;
  void CheckResidency(uint64_t encoderId,
                      MTL_BINDABLE_RESIDENCY_MASK residencyMask,
                      MTL_BINDABLE_RESIDENCY_MASK *newResidencyMask) {
    if (encoderId > last_encoder_id) {
      last_residency_mask = residencyMask;
      last_encoder_id = encoderId;
      *newResidencyMask = last_residency_mask;
      return;
    }
    if (encoderId == last_encoder_id) {
      if ((last_residency_mask & residencyMask) == residencyMask) {
        // it's already resident
        *newResidencyMask = MTL_RESIDENCY_NULL;
        return;
      }
      last_residency_mask |= residencyMask;
      *newResidencyMask = last_residency_mask;
      return;
    }
    // invalid
    *newResidencyMask = MTL_RESIDENCY_NULL;
  };
};

struct SIMPLE_OCCUPANCY_TRACKER {
  uint64_t last_used_seq = 0;
  void MarkAsOccupied(uint64_t seq_id) {
    last_used_seq = std::max(last_used_seq, seq_id);
  }
  bool IsOccupied(uint64_t finished_seq_id) {
    return last_used_seq > finished_seq_id;
  }
};

/**
FIXME: don't hold a IMTLBindable in any places other than context state.
especially don't capture it in command lambda.
 */
DEFINE_COM_INTERFACE("1c7e7c98-6dd4-42f0-867b-67960806886e", IMTLBindable)
    : public IUnknown {
  /**
  Semantic: get a reference of the underlying metal resource and
  other necessary information (e.g. size),record the time/seqId
  of usage (no matter read/write)
   */
  virtual dxmt::BindingRef UseBindable(uint64_t bindAtSeqId) = 0;
  /**
  Semantic: 1. get all data required to fill the argument buffer
  it's similar to `UseBindable`, but optimized for setting
  argument buffer (which is frequent and needs to be performant!)
  so it provides raw gpu handle/resource_id without extra reference
  (no need to worry about missing reference, because `UseBindable`
  will be called at least once for marking residency anyway)
  2. provide current encoder id and intended residency state, get
  the latest residency state in current encoder, or NULL if there
  is no need to change residency state (in case it's already resident
  before)
   */
  virtual dxmt::ArgumentData GetArgumentData(SIMPLE_RESIDENCY_TRACKER *
                                             *ppTracker) = 0;
  /**
  Semantic: check if the resource is used by gpu at the moment
  it guarantees true negative but can give false positive!
  thus only use it as a potential to optimize updating from CPU
  generally assume it return `true`
   */
  virtual bool GetContentionState(uint64_t finishedSeqId) = 0;
  /**
  Usually it's effectively just QueryInterface, except for dynamic resources
   */
  virtual void GetLogicalResourceOrView(REFIID riid,
                                        void **ppLogicalResource) = 0;
};

DEFINE_COM_INTERFACE("daf21510-d136-44dd-bb16-068a94690775",
                     IMTLD3D11BackBuffer)
    : public IUnknown {
  virtual void Present(MTL::CommandBuffer * cmdbuf, double vsync_duration) = 0;
  virtual void Destroy() = 0;
};

DEFINE_COM_INTERFACE("65feb8c5-01de-49df-bf58-d115007a117d", IMTLDynamicBuffer)
    : public IUnknown {
  virtual void *GetMappedMemory(UINT * pBytesPerRow, UINT * pBytesPerImage) = 0;
  virtual UINT GetSize(UINT * pBytesPerRow, UINT * pBytesPerImage) = 0;
  virtual void RotateBuffer(dxmt::MTLD3D11Device * pool) = 0;
  virtual dxmt::BindingRef GetCurrentBufferBinding() = 0;
  virtual D3D11_BIND_FLAG GetBindFlag() = 0;
};

DEFINE_COM_INTERFACE("252c1a0e-1c61-42e7-9b57-23dfe3d73d49", IMTLD3D11Staging)
    : public IUnknown {

  virtual bool UseCopyDestination(
      uint32_t Subresource, uint64_t seq_id, MTL_STAGING_RESOURCE * pBuffer,
      uint32_t * pBytesPerRow, uint32_t * pBytesPerImage) = 0;
  virtual bool UseCopySource(
      uint32_t Subresource, uint64_t seq_id, MTL_STAGING_RESOURCE * pBuffer,
      uint32_t * pBytesPerRow, uint32_t * pBytesPerImage) = 0;
  /**
  cpu_coherent_seq_id: any operation at/before seq_id is coherent to cpu
  -
  return:
  = 0 - map success
  > 0 - resource in use
  < 0 - error (?)
  */
  virtual int64_t TryMap(uint32_t Subresource, uint64_t cpu_coherent_seq_id,
                         // uint64_t commited_seq_id,
                         D3D11_MAP flag,
                         D3D11_MAPPED_SUBRESOURCE * pMappedResource) = 0;
  virtual void Unmap(uint32_t Subresource) = 0;
};

DEFINE_COM_INTERFACE("9a6f6549-d4b1-45ea-8794-8503d190d3d1",
                     IMTLMinLODClampable)
    : public IUnknown {
  virtual void SetMinLOD(float MinLOD) = 0;
  virtual float GetMinLOD() = 0;
};

namespace dxmt {

template <typename RESOURCE_DESC>
void UpgradeResourceDescription(const RESOURCE_DESC *pSrc, RESOURCE_DESC &dst) {
  dst = *pSrc;
}
template <typename RESOURCE_DESC>
void DowngradeResourceDescription(const RESOURCE_DESC &src,
                                  RESOURCE_DESC *pDst) {
  *pDst = src;
}

template <typename RESOURCE_DESC_SRC, typename RESOURCE_DESC_DST>
void UpgradeResourceDescription(const RESOURCE_DESC_SRC *pSrc,
                                RESOURCE_DESC_DST &dst);
template <typename RESOURCE_DESC_SRC, typename RESOURCE_DESC_DST>
void DowngradeResourceDescription(const RESOURCE_DESC_SRC &src,
                                  RESOURCE_DESC_DST *pDst);

template <typename VIEW_DESC>
void UpgradeViewDescription(const VIEW_DESC *pSrc, VIEW_DESC &dst) {
  dst = *pSrc;
}
template <typename VIEW_DESC>
void DowngradeViewDescription(const VIEW_DESC &src, VIEW_DESC *pDst) {
  *pDst = src;
}

template <typename VIEW_DESC_SRC, typename VIEW_DESC_DST>
void UpgradeViewDescription(const VIEW_DESC_SRC *pSrc, VIEW_DESC_DST &dst);
template <typename VIEW_DESC_SRC, typename VIEW_DESC_DST>
void DowngradeViewDescription(const VIEW_DESC_SRC &src, VIEW_DESC_DST *pDst);

struct tag_buffer {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_BUFFER;
  using COM = ID3D11Buffer;
  using COM_IMPL = ID3D11Buffer;
  using DESC = D3D11_BUFFER_DESC;
  using DESC1 = D3D11_BUFFER_DESC;
  static constexpr std::string_view debug_name = "buffer";
};

struct tag_texture_1d {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_TEXTURE1D;
  using COM = ID3D11Texture1D;
  using COM_IMPL = ID3D11Texture1D;
  using DESC = D3D11_TEXTURE1D_DESC;
  using DESC1 = D3D11_TEXTURE1D_DESC;
  static constexpr std::string_view debug_name = "tex1d";
};

struct tag_texture_2d {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_TEXTURE2D;
  using COM = ID3D11Texture2D;
  using COM_IMPL = ID3D11Texture2D1;
  using DESC = D3D11_TEXTURE2D_DESC;
  using DESC1 = D3D11_TEXTURE2D_DESC1;
  static constexpr std::string_view debug_name = "tex2d";
};

struct tag_texture_3d {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_TEXTURE3D;
  using COM = ID3D11Texture3D;
  using COM_IMPL = ID3D11Texture3D1;
  using DESC = D3D11_TEXTURE3D_DESC;
  using DESC1 = D3D11_TEXTURE3D_DESC1;
  static constexpr std::string_view debug_name = "tex3d";
};

template <typename tag, typename... Base>
class TResourceBase : public MTLD3D11DeviceChild<typename tag::COM_IMPL,
                                                 IDXMTResource, Base...> {
public:
  TResourceBase(const tag::DESC1 &desc, MTLD3D11Device *device)
      : MTLD3D11DeviceChild<typename tag::COM_IMPL, IDXMTResource, Base...>(
            device),
        desc(desc),
        dxgi_resource(new MTLDXGIResource<TResourceBase<tag, Base...>>(this)) {}

  template <std::size_t n> HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    return E_NOINTERFACE;
  };

  template <std::size_t n, typename V, typename... Args>
  HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    if (riid == __uuidof(V)) {
      *ppvObject = ref_and_cast<V>(this);
      return S_OK;
    }
    return ResolveBase<n + 1, Args...>(riid, ppvObject);
  };

  HRESULT QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    auto hr = ResolveBase<0, Base...>(riid, ppvObject);
    if (SUCCEEDED(hr)) {
      return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Resource) ||
        riid == __uuidof(typename tag::COM) ||
        riid == __uuidof(typename tag::COM_IMPL)) {
      *ppvObject = ref_and_cast<typename tag::COM_IMPL>(this);
      return S_OK;
    }

    if (riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGIResource) || riid == __uuidof(IDXGIResource1)) {
      *ppvObject = ref(dxgi_resource.get());
      return S_OK;
    }

    if (riid == __uuidof(IDXMTResource)) {
      *ppvObject = ref_and_cast<IDXMTResource>(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLDynamicBuffer) || riid == __uuidof(IMTLBindable) ||
        riid == __uuidof(IMTLD3D11Staging)) {
      // silent these interfaces
      return E_NOINTERFACE;
    }

    if (logQueryInterfaceError(__uuidof(typename tag::COM), riid)) {
      WARN("D3D11Resource(", tag::debug_name ,"): Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void GetDesc(tag::DESC *pDesc) final {
    ::dxmt::DowngradeResourceDescription(desc, pDesc);
  }

  void GetDesc1(tag::DESC1 *pDesc) /* override / final */ { *pDesc = desc; }

  void GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension) final {
    *pResourceDimension = tag::dimension;
  }

  void SetEvictionPriority(UINT EvictionPriority) final {}

  UINT GetEvictionPriority() final { return DXGI_RESOURCE_PRIORITY_NORMAL; }

  virtual HRESULT GetDeviceInterface(REFIID riid, void **ppDevice) {
    Com<ID3D11Device> device;
    this->GetDevice(&device);
    return device->QueryInterface(riid, ppDevice);
  };

  virtual HRESULT GetDXGIUsage(DXGI_USAGE *pUsage) {
    if (!pUsage) {
      return E_INVALIDARG;
    }
    *pUsage = 0;
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc,
                           ID3D11ShaderResourceView1 **ppView) {
    return E_INVALIDARG;
  };
  virtual HRESULT STDMETHODCALLTYPE
  CreateUnorderedAccessView(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc,
                            ID3D11UnorderedAccessView1 **ppView) {
    return E_INVALIDARG;
  };
  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTargetView(const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc,
                         ID3D11RenderTargetView1 **ppView) {
    return E_INVALIDARG;
  };
  virtual HRESULT STDMETHODCALLTYPE
  CreateDepthStencilView(const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
                         ID3D11DepthStencilView **ppView) {
    return E_INVALIDARG;
  };

protected:
  tag::DESC1 desc;
  std::unique_ptr<IDXGIResource1> dxgi_resource;
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = IMTLD3D11RenderTargetView>
struct tag_render_target_view {
  using COM = ID3D11RenderTargetView;
  using COM1 = ID3D11RenderTargetView1;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_RENDER_TARGET_VIEW_DESC;
  using DESC1 = D3D11_RENDER_TARGET_VIEW_DESC1;
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = IMTLD3D11DepthStencilView>
struct tag_depth_stencil_view {
  using COM = ID3D11DepthStencilView;
  using COM1 = ID3D11DepthStencilView;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_DEPTH_STENCIL_VIEW_DESC;
  using DESC1 = D3D11_DEPTH_STENCIL_VIEW_DESC;
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = IMTLD3D11ShaderResourceView>
struct tag_shader_resource_view {
  using COM = ID3D11ShaderResourceView;
  using COM1 = ID3D11ShaderResourceView1;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_SHADER_RESOURCE_VIEW_DESC;
  using DESC1 = D3D11_SHADER_RESOURCE_VIEW_DESC1;
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = IMTLD3D11UnorderedAccessView>
struct tag_unordered_access_view {
  using COM = ID3D11UnorderedAccessView;
  using COM1 = ID3D11UnorderedAccessView1;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_UNORDERED_ACCESS_VIEW_DESC;
  using DESC1 = D3D11_UNORDERED_ACCESS_VIEW_DESC1;
};

template <typename tag, typename... Base>
class TResourceViewBase
    : public MTLD3D11DeviceChild<typename tag::COM_IMPL, Base...> {
public:
  TResourceViewBase(const tag::DESC1 *pDesc, tag::RESOURCE_IMPL *pResource,
                    MTLD3D11Device *device)
      : MTLD3D11DeviceChild<typename tag::COM_IMPL, Base...>(device),
        resource(pResource) {
    if (pDesc) {
      desc = *pDesc;
    }
  }

  template <std::size_t n> HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    return E_NOINTERFACE;
  };

  template <std::size_t n, typename V, typename... Args>
  HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    if (riid == __uuidof(V)) {
      *ppvObject = ref_and_cast<V>(this);
      return S_OK;
    }
    return ResolveBase<n + 1, Args...>(riid, ppvObject);
  };

  HRESULT QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    auto hr = ResolveBase<0, Base...>(riid, ppvObject);
    if (SUCCEEDED(hr)) {
      return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11View) || riid == __uuidof(typename tag::COM) ||
        riid == __uuidof(typename tag::COM1) ||
        riid == __uuidof(typename tag::COM_IMPL)) {
      *ppvObject = ref_and_cast<typename tag::COM_IMPL>(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLDynamicBuffer) || riid == __uuidof(IMTLBindable)) {
      // silent these interfaces
      return E_NOINTERFACE;
    }

    if (logQueryInterfaceError(__uuidof(typename tag::COM_IMPL), riid)) {
      WARN("D3D11View: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void GetDesc(tag::DESC *pDesc) final {
    DowngradeViewDescription(desc, pDesc);
  }

  void GetDesc1(tag::DESC1 *pDesc) /* override / final */ { *pDesc = desc; }

  void GetResource(tag::RESOURCE **ppResource) final {
    resource->QueryInterface(IID_PPV_ARGS(ppResource));
  }

  virtual ULONG64 GetUnderlyingResourceId() { return (ULONG64)resource.ptr(); };

  virtual dxmt::ResourceSubset GetViewRange() { return ResourceSubset(desc); };

  virtual bool GetContentionState(uint64_t finishedSeqId) { return true; };

protected:
  tag::DESC1 desc;
  /**
  strong ref to resource
  */
  Com<typename tag::RESOURCE_IMPL> resource;
};

#pragma region Resource Factory

HRESULT
CreateStagingBuffer(MTLD3D11Device *pDevice, const D3D11_BUFFER_DESC *pDesc,
                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                    ID3D11Buffer **ppBuffer);

HRESULT
CreateStagingTexture1D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE1D_DESC *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture1D **ppTexture);

HRESULT
CreateStagingTexture2D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE2D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture2D1 **ppTexture);

HRESULT
CreateStagingTexture3D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE3D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture3D1 **ppTexture);

HRESULT
CreateDeviceBuffer(MTLD3D11Device *pDevice, const D3D11_BUFFER_DESC *pDesc,
                   const D3D11_SUBRESOURCE_DATA *pInitialData,
                   ID3D11Buffer **ppBuffer);

HRESULT CreateDeviceTexture1D(MTLD3D11Device *pDevice,
                              const D3D11_TEXTURE1D_DESC *pDesc,
                              const D3D11_SUBRESOURCE_DATA *pInitialData,
                              ID3D11Texture1D **ppTexture);

HRESULT CreateDeviceTexture2D(MTLD3D11Device *pDevice,
                              const D3D11_TEXTURE2D_DESC1 *pDesc,
                              const D3D11_SUBRESOURCE_DATA *pInitialData,
                              ID3D11Texture2D1 **ppTexture);

HRESULT CreateDeviceTexture3D(MTLD3D11Device *pDevice,
                              const D3D11_TEXTURE3D_DESC1 *pDesc,
                              const D3D11_SUBRESOURCE_DATA *pInitialData,
                              ID3D11Texture3D1 **ppTexture);

HRESULT
CreateDynamicBuffer(MTLD3D11Device *pDevice, const D3D11_BUFFER_DESC *pDesc,
                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                    ID3D11Buffer **ppBuffer);

HRESULT CreateDynamicTexture2D(MTLD3D11Device *pDevice,
                               const D3D11_TEXTURE2D_DESC1 *pDesc,
                               const D3D11_SUBRESOURCE_DATA *pInitialData,
                               ID3D11Texture2D1 **ppTexture);
#pragma endregion

#pragma region Helper
template <typename RESOURCE_DESC, typename VIEW_DESC>
HRESULT ExtractEntireResourceViewDescription(const RESOURCE_DESC *pResourceDesc,
                                             VIEW_DESC *pViewDescOut);

template <typename RESOURCE_DESC, typename VIEW_DESC>
HRESULT ExtractEntireResourceViewDescription(const RESOURCE_DESC *pResourceDesc,
                                             const VIEW_DESC *pViewDescIn,
                                             VIEW_DESC *pViewDescOut) {
  if (pViewDescIn) {
    *pViewDescOut = *pViewDescIn;
    if constexpr (!std::is_same_v<RESOURCE_DESC, D3D11_BUFFER_DESC>) {
      if (pViewDescOut->Format == DXGI_FORMAT_UNKNOWN) {
        pViewDescOut->Format = pResourceDesc->Format;
      }
    }
    return S_OK;
  } else {
    return ExtractEntireResourceViewDescription(pResourceDesc, pViewDescOut);
  }
}

template <typename TEXTURE_DESC>
HRESULT CreateMTLTextureDescriptor(MTLD3D11Device *pDevice,
                                   const TEXTURE_DESC *pDesc,
                                   TEXTURE_DESC *pOutDesc,
                                   MTL::TextureDescriptor **pMtlDescOut);

template <typename VIEW_DESC>
HRESULT CreateMTLTextureView(MTLD3D11Device *pDevice, MTL::Texture *pResource,
                             const VIEW_DESC *pViewDesc, MTL::Texture **ppView);

HRESULT
CreateMTLRenderTargetView(MTLD3D11Device *pDevice, MTL::Texture *pResource,
                          const D3D11_RENDER_TARGET_VIEW_DESC1 *pViewDesc,
                          MTL::Texture **ppView,
                          MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc);

HRESULT
CreateMTLDepthStencilView(MTLD3D11Device *pDevice, MTL::Texture *pResource,
                          const D3D11_DEPTH_STENCIL_VIEW_DESC *pViewDesc,
                          MTL::Texture **ppView,
                          MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc);

struct MTL_TEXTURE_BUFFER_LAYOUT {
  uint32_t ByteOffset;
  uint32_t ByteWidth;
  uint32_t ViewElementOffset;
  uint32_t AdjustedByteOffset;
  uint32_t AdjustedBytesPerRow;
};

template <typename VIEW_DESC>
HRESULT CreateMTLTextureBufferView(MTLD3D11Device *pDevice,
                                   const VIEW_DESC *pViewDesc,
                                   MTL::TextureDescriptor **ppViewDesc,
                                   MTL_TEXTURE_BUFFER_LAYOUT *pLayout);

template <typename TEXTURE_DESC>
void GetMipmapSize(const TEXTURE_DESC *pDesc, uint32_t level, uint32_t *pWidth,
                   uint32_t *pHeight, uint32_t *pDepth);

template <typename TEXTURE_DESC>
HRESULT GetLinearTextureLayout(MTLD3D11Device *pDevice,
                               const TEXTURE_DESC *pDesc, uint32_t level,
                               uint32_t *pBytesPerRow, uint32_t *pBytesPerImage,
                               uint32_t *pBytesPerSlice);

constexpr void
CalculateBufferViewOffsetAndSize(const D3D11_BUFFER_DESC &buffer_desc,
                                 uint32_t element_stride,
                                 uint32_t first_element, uint32_t num_elements,
                                 uint32_t &offset, uint32_t &size) {
  offset = first_element * element_stride;
  size = std::min(std::max(0u, buffer_desc.ByteWidth - offset),
                  element_stride * num_elements);
};
#pragma endregion

} // namespace dxmt