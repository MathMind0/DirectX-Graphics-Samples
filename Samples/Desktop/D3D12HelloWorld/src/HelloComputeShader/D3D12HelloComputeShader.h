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

// Enables the Nsight Aftermath code instrumentation for GPU crash dump creation.
#define USE_NSIGHT_AFTERMATH 1

#if defined(USE_NSIGHT_AFTERMATH)
#include "NsightAftermathGpuCrashTracker.h"
#endif

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloConstBuffers : public DXSample
{
public:
    D3D12HelloConstBuffers(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    static const UINT FrameCount = 2;

    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 color;
    };

    struct InitBlocksConstantBuffer
    {
        XMUINT4 nTiles;
        XMFLOAT4 blockWidth;
        float padding[56]; // Padding so the constant buffer is 256-byte aligned.
    };
    static_assert((sizeof(InitBlocksConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    UINT m_rtvDescriptorSize;
    
    ComPtr<ID3D12DescriptorHeap> m_heapDescriptors;
    UINT m_descriptorSize;

    enum
    {
        OFFSET_VERTEX_BUFFER_UAV,
        OFFSET_VELOCITY_BUFFER_SRV,
        OFFSET_VELOCITY_BUFFER_UAV,
        DESCRIPTOR_NUM
    };
    
    ComPtr<ID3D12RootSignature> m_rootSignatureInitBlocks;
    ComPtr<ID3D12PipelineState> m_pipelineStateInitBlocks;
    ComPtr<ID3D12Resource> m_constantBufferInitBlocks;
    InitBlocksConstantBuffer m_constantBufferInitBlocksData;
    
    ComPtr<ID3D12RootSignature> m_rootSignatureDraw;
    ComPtr<ID3D12PipelineState> m_pipelineStateDraw;
    
    // App resources.
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_velocityBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    bool m_needInit;
    
    static const UINT TILE_NUM = 16; 

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

#if defined(USE_NSIGHT_AFTERMATH)
    // App-managed marker functionality
    UINT64 m_frameCounter;
    GpuCrashTracker::MarkerMap m_markerMap;

    // Nsight Aftermath instrumentation
    GFSDK_Aftermath_ContextHandle m_hAftermathCommandListContext;
    GpuCrashTracker m_gpuCrashTracker;
#endif
    
    void LoadPipeline();
    void LoadAssets();
    void PopulateCommandList();
    void WaitForPreviousFrame();
};
