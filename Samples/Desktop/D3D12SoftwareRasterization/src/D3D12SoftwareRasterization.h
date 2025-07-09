//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXSample.h"
#include "SimpleCamera.h"
#include "StepTimer.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12SoftwareRasterization : public DXSample
{
public:
    D3D12SoftwareRasterization(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown(UINT8 key);
    virtual void OnKeyUp(UINT8 key);

private:
    static constexpr UINT FrameCount = 2;
    UINT m_frameWidth, m_frameHeight;
    static constexpr float FrameScale = 8.f;

    struct alignas(256) ConstantBuffer
    {
        XMFLOAT4X4 matMVP;
        XMUINT2 szCanvas;
        XMUINT2 numTriangles;
        // Constant buffers are 256-byte aligned in GPU memory. Padding is added
        // for convenience when computing the struct's size.
        //float padding[32];
    };
    static_assert((sizeof(ConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

    struct Vertex
    {
        XMFLOAT3 position;
    };
    
    // Pipeline objects.
    ComPtr<IDXGIFactory4> m_dxgiFactory;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;

    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    D3D12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
        
    ComPtr<ID3D12CommandAllocator> m_commandAllocatorGraphics[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueueGraphics;
    ComPtr<ID3D12GraphicsCommandList> m_commandListGraphics;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
    UINT m_rtvDescriptorSize;
    UINT m_srvUavDescriptorSize;

    // Asset objects.
    ComPtr<ID3D12RootSignature> m_sigRasterize;
    ComPtr<ID3D12PipelineState> m_psoRasterize;
    ComPtr<ID3D12RootSignature> m_sigCopyToRT;
    ComPtr<ID3D12PipelineState> m_psoCopyToRT;

    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_vertexBufferUpload;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvVertexBufferGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvVertexBufferCpu;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_indexBufferUpload;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvIndexBufferGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvIndexBufferCpu;
    UINT m_numTriangles;
    
    ComPtr<ID3D12Resource> m_constantBuffer[FrameCount];
    ConstantBuffer* m_pConstantBufferData[FrameCount];

    ComPtr<ID3D12Resource> m_texRasterCanvas;
    D3D12_GPU_DESCRIPTOR_HANDLE m_uavRasterCanvasGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavRasterCanvasCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvRasterCanvasGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvRasterCanvasCpu;
    
    UINT m_frameIndex;
    SimpleCamera m_camera;
    StepTimer m_timer;

    // Synchronization objects.
    ComPtr<ID3D12Fence> m_renderContextFence;
    UINT64 m_currentFenceValue;
    HANDLE m_renderContextFenceEvent;
    UINT64 m_frameFenceValues[FrameCount];

    // Indices of the root signature parameters.
    enum ComputeRootParameters : UINT32
    {
        ComputeRootCBV = 0,
        ComputeRootSRVTable,
        ComputeRootUAVTable,
        ComputeRootParametersCount
    };

    // Indices of shader resources in the descriptor heap.
    enum DescriptorHeapIndex : UINT32
    {
        SRV_VERTEX_BUFFER,
        SRV_INDEX_BUFFER,
        SRV_FRAME_BUFFER,
        UAV_FRAME_BUFFER,
        DescriptorCount
    };

    void LoadPipeline();
    void CreateConstantBuffer();
    void CreateRTs();
    void CreatePSOs();
    void LoadAssets();
    void RestoreD3DResources();
    void ReleaseD3DResources();
    void CreateMeshBuffers();
    void CreateFrameBuffer();
    void PopulateCommandList();
    void FlushGPU();
    void MoveToNextFrame();
};
