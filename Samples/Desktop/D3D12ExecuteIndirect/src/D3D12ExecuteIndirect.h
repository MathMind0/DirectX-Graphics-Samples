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

using namespace std;
using namespace std::chrono;

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

// We pack the UAV counter into the same buffer as the commands rather than create
// a separate 64K resource/heap for it. The counter must be aligned on 4K boundaries,
// so we pad the command buffer (if necessary) such that the counter will be placed
// at a valid location in the buffer.
UINT inline constexpr AlignForUavCounter(UINT bufferSize)
{
    const UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
    return (bufferSize + (alignment - 1)) & ~(alignment - 1);
}

class D3D12ExecuteIndirect : public DXSample
{
public:
    D3D12ExecuteIndirect(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown(UINT8 key);

private:
    static constexpr UINT FrameCount = 2;
    static constexpr UINT TriangleCount = 1000000;    
    static constexpr UINT ComputeThreadBlockSize = 64;       // Should match the value in compute.hlsl.
    static constexpr float TriangleHalfWidth = 0.01f;        // The x and y offsets used by the triangle vertices.
    static constexpr XMFLOAT4 CullBox = XMFLOAT4(-0.5f, -0.5f, 0.5f, 0.5f);

    // Vertex definition.
    struct Vertex
    {
        XMFLOAT3 position;
    };

    // Constant buffer definition.
    struct PrimitiveStaticDataCB
    {
        XMFLOAT4 color;        
        XMFLOAT2 velocity;
        float    size;
        float    rotation;

        // Constant buffers are 256-byte aligned. Add padding in the struct to allow multiple buffers
        // to be array-indexed.
        float padding[256 - 4 * 8];
    };

    // Root constants for the compute shader.
    struct CSRootConstants
    {
        XMFLOAT4 cullBox;
        float commandCount;
        float deltaTime;
    };
    constexpr static UINT COMPUTE_CONST_VALUE_SLOTS = sizeof(CSRootConstants) / 4;
    
    // Data structure to match the command signature used for ExecuteIndirect.
    struct PrimitiveDynamicDataCB
    {
        XMFLOAT2 position;  
        float angle;
        float padding;
    };
    constexpr static UINT GRAPHICS_DYNAMIC_DATA_CONST_VALUES = sizeof(PrimitiveDynamicDataCB) / 4;

    struct ViewDataCB
    {
        float ratio;
    };
    constexpr static UINT GRAPHICS_VIEW_DATA_CONST_VALUES = sizeof(ViewDataCB) / 4;
    
    struct IndirectCommand
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbStaticData;
        PrimitiveDynamicDataCB cbDynamicData;
        D3D12_DRAW_ARGUMENTS drawArguments;
    };


    // The size of the indirect commands to draw all of the triangles in a single frame.
    static constexpr UINT CommandSizePerFrame = TriangleCount * sizeof(IndirectCommand);               
    // The offset of the UAV counter in the processed command buffer.
    static constexpr UINT CommandBufferCounterOffset = AlignForUavCounter(CommandSizePerFrame); 

    // Graphics root signature parameter offsets.
    enum GraphicsRootParameters
    {
        StaticData,
        DynmicData,
        ViewData,
        GraphicsRootParametersCount
    };

    // Compute root signature parameter offsets.
    enum ComputeRootParameters
    {
        SrvUavTable,
        RootConstants,            // Root constants that give the shader information about the triangle vertices and culling planes.
        ComputeRootParametersCount
    };

    // CBV/SRV/UAV desciptor heap offsets.
    enum HeapOffsets
    {
        CbvSrvOffset = 0,                                                    // SRV that points to the constant buffers used by the rendering thread.
        CommandsOffset = CbvSrvOffset + 1,                                   // UAV that points to all of the indirect commands.
        ProcessedCommandsOffset = CommandsOffset + 1,                        // UAV that records the commands we actually want to execute.
        CbvSrvUavDescriptorCountPerFrame = ProcessedCommandsOffset + 1       // 1 SRV + 2 UAVs for the compute shader.
    };

    // Each triangle gets its own constant buffer per frame.
    std::vector<PrimitiveStaticDataCB> m_constantBufferData;
    UINT8* m_pCbvDataBegin;

    CSRootConstants m_csRootConstants;    // Constants for the compute shader.

    ViewDataCB m_viewDataCB;
    
    bool m_enableCulling;                // Toggle whether the compute shader pre-processes the indirect commands.

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    D3D12_RECT m_cullingScissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12RootSignature> m_computeRootSignature;
    ComPtr<ID3D12CommandSignature> m_commandSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;
    UINT m_rtvDescriptorSize;
    UINT m_cbvSrvUavDescriptorSize;
    UINT m_frameIndex;
    time_point<high_resolution_clock> m_lastFrameTime;

    // Synchronization objects.
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FrameCount];
    HANDLE m_fenceEvent;

    // Asset objects.
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12PipelineState> m_computeState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_constantBuffer;
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12Resource> m_commandBuffer;
    ComPtr<ID3D12Resource> m_processedCommandBuffer;
    ComPtr<ID3D12Resource> m_processedCommandBufferCounterReset;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    void LoadPipeline();
    void LoadAssets();
    void RestoreD3DResources();
    void ReleaseD3DResources();
    float GetRandomFloat(float min, float max);
    void PopulateCommandLists();
    void WaitForGpu();
    void MoveToNextFrame();
};
