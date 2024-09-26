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

#include "stdafx.h"
#include "FrameResource.h"
#include "SquidRoom.h"

FrameResource::FrameResource(ID3D12Device* pDevice,
    ID3D12Resource* pBackBuffer, const D3D12_CPU_DESCRIPTOR_HANDLE& rtvBackBuffer,
    const D3D12_CPU_DESCRIPTOR_HANDLE& dsvDepthStencil,
    ID3D12PipelineState* ppsoRenderScene, ID3D12PipelineState* ppsoRenderShadow,
    ID3D12RootSignature* psigRenderScene, ID3D12RootSignature* psigRenderShadow,
    ID3D12DescriptorHeap* pRtvHeap, ID3D12DescriptorHeap* pDsvHeap,
    ID3D12DescriptorHeap* pCbvSrvHeap, ID3D12DescriptorHeap* pSamplerHeap,
    const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissorRect,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
    UINT cbvSrvDescriptorSize, const D3D12_GPU_DESCRIPTOR_HANDLE& srvTextureStart,
    UINT frameResourceIndex) :
    m_backBuffer(pBackBuffer),
    m_rtvBackBuffer(rtvBackBuffer),
    m_dsvDepthStencil(dsvDepthStencil),
    m_psoRenderScene(ppsoRenderScene),
    m_psoRenderShadow(ppsoRenderShadow),
    m_sigRenderScene(psigRenderScene),
    m_sigRenderShadow(psigRenderShadow),
    m_samplerHeap(pSamplerHeap),
    m_viewport(viewport),
    m_scissorRect(scissorRect),
    m_vertexBufferView(vertexBufferView),
    m_indexBufferView(indexBufferView),
    m_cbvSrvDescriptorSize(cbvSrvDescriptorSize),
    m_srvTextureStart(srvTextureStart),
    fenceValue(0)
{
    ThrowIfFailed(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(commandAllocator)));
    ThrowIfFailed(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(), m_psoRenderScene.Get(),
            IID_PPV_ARGS(commandList)));

    NAME_D3D12_OBJECT(commandAllocator);
    NAME_D3D12_OBJECT(commandList);

    // Close these command lists; don't record into them for now.
    ThrowIfFailed(commandList->Close());

    // Describe and create the shadow map texture.
    CD3DX12_RESOURCE_DESC shadowTexDesc(
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        static_cast<UINT>(viewport.Width), 
        static_cast<UINT>(viewport.Height), 
        1,
        1,
        DXGI_FORMAT_R32_TYPELESS,
        1, 
        0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clearValue; // Performance tip: Tell the runtime at resource creation the desired clear value.
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &shadowTexDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowTexture)));

    NAME_D3D12_OBJECT(m_shadowTexture);

    // Get a handle to the start of the descriptor heap then offset 
    // it based on the frame resource index.
    const UINT dsvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE depthHandle(pDsvHeap->GetCPUDescriptorHandleForHeapStart(),
        1 + frameResourceIndex, dsvDescriptorSize); // + 1 for the shadow map.

    // Describe and create the shadow depth view and cache the CPU 
    // descriptor handle.
    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
    depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Texture2D.MipSlice = 0;
    pDevice->CreateDepthStencilView(m_shadowTexture.Get(), &depthStencilViewDesc, depthHandle);
    m_shadowDepthView = depthHandle;

    // Get a handle to the start of the descriptor heap then offset it 
    // based on the existing textures and the frame resource index. Each 
    // frame has 1 SRV (shadow tex) and 2 CBVs.
    const UINT nullSrvCount = 2;   // Null descriptors at the start of the heap.
    // Diffuse + normal textures near the start of the heap.
    // Ideally, track descriptor heap contents/offsets at a higher level.
    const UINT textureCount = _countof(SampleAssets::Textures);    
    const UINT cbvSrvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvCpuHandle(pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvGpuHandle(pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
    m_nullSrvHandle = cbvSrvGpuHandle;
    cbvSrvCpuHandle.Offset(nullSrvCount + textureCount + (frameResourceIndex * NUM_DESCRIPTORS), cbvSrvDescriptorSize);
    cbvSrvGpuHandle.Offset(nullSrvCount + textureCount + (frameResourceIndex * NUM_DESCRIPTORS), cbvSrvDescriptorSize);

    // Describe and create a shader resource view (SRV) for the shadow depth 
    // texture and cache the GPU descriptor handle. This SRV is for sampling 
    // the shadow map from our shader. It uses the same texture that we use 
    // as a depth-stencil during the shadow pass.
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
    shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadowSrvDesc.Texture2D.MipLevels = 1;
    shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    pDevice->CreateShaderResourceView(m_shadowTexture.Get(), &shadowSrvDesc, cbvSrvCpuHandle);
    m_shadowDepthHandle = cbvSrvGpuHandle;

    // Increment the descriptor handles.
    cbvSrvCpuHandle.Offset(cbvSrvDescriptorSize);
    cbvSrvGpuHandle.Offset(cbvSrvDescriptorSize);

    // Create the constant buffers.
    const UINT constantBufferSize =
        (sizeof(SceneConstantBuffer) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1))
        & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1); // must be a multiple 256 bytes
    
    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&cbShadow)));
    
    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&cbScene)));

    // Map the constant buffers and cache their heap pointers.
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(cbShadow->Map(0, &readRange,
        reinterpret_cast<void**>(&pShadowData)));
    ThrowIfFailed(cbScene->Map(0, &readRange,
        reinterpret_cast<void**>(&pSceneData)));

    // Create the constant buffer views: one for the shadow pass and
    // another for the scene pass.
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.SizeInBytes = constantBufferSize;

    // Describe and create the shadow constant buffer view (CBV) and 
    // cache the GPU descriptor handle.
    cbvDesc.BufferLocation = cbShadow->GetGPUVirtualAddress();
    pDevice->CreateConstantBufferView(&cbvDesc, cbvSrvCpuHandle);
    cbvShadow = cbvSrvGpuHandle;

    // Increment the descriptor handles.
    cbvSrvCpuHandle.Offset(cbvSrvDescriptorSize);
    cbvSrvGpuHandle.Offset(cbvSrvDescriptorSize);

    // Describe and create the scene constant buffer view (CBV) and 
    // cache the GPU descriptor handle.
    cbvDesc.BufferLocation = cbScene->GetGPUVirtualAddress();
    pDevice->CreateConstantBufferView(&cbvDesc, cbvSrvCpuHandle);
    cbvScene = cbvSrvGpuHandle;

    // Increment the descriptor handles.
    cbvSrvCpuHandle.Offset(cbvSrvDescriptorSize);
    cbvSrvGpuHandle.Offset(cbvSrvDescriptorSize);

    // Resource initialization for postprocess.
    CD3DX12_RESOURCE_DESC descSceneColor = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        static_cast<UINT>(viewport.Width),
        static_cast<UINT>(viewport.Height),
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    FLOAT clearColor[] = {0.f, 0.f, 0.f, 0.f};
    CD3DX12_CLEAR_VALUE clearSceneColor(DXGI_FORMAT_R8G8B8A8_UNORM,
        clearColor);
        
    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &descSceneColor,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearSceneColor,
        IID_PPV_ARGS(&m_texSceneColor)));

    NAME_D3D12_OBJECT(m_texSceneColor);

    m_srvSceneColorCpu = cbvSrvCpuHandle;
    m_srvSceneColorGpu = cbvSrvGpuHandle;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvSceneColorDesc = {};
    srvSceneColorDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvSceneColorDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvSceneColorDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvSceneColorDesc.Texture2D.MipLevels = 1;

    pDevice->CreateShaderResourceView(m_texSceneColor.Get(), &srvSceneColorDesc, m_srvSceneColorCpu);

    const UINT rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_rtvSceneColorCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(pRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        FrameCount + frameResourceIndex, rtvDescriptorSize);
    m_rtvSceneColorGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(pRtvHeap->GetGPUDescriptorHandleForHeapStart(),
        FrameCount + frameResourceIndex, rtvDescriptorSize);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_UNKNOWN;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    pDevice->CreateRenderTargetView(m_texSceneColor.Get(), &rtvDesc, m_rtvSceneColorCpu);
    
    const UINT szScreenInfoCB = (sizeof(ScreenInfo) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) &
        ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        CD3DX12_RESOURCE_DESC::Buffer(szScreenInfoCB),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(cbScreenInfo)));

    ThrowIfFailed(cbScreenInfo->Map(0, &readRange,
        reinterpret_cast<void**>(&pScreenInfoData)));
}

FrameResource::~FrameResource()
{
    m_shadowTexture = nullptr;
    m_texSceneColor = nullptr;
}

// Builds and writes constant buffers from scratch to the proper slots for 
// this frame resource.
void FrameResource::WriteConstantBuffers(const D3D12_VIEWPORT& viewport, Camera* pSceneCamera,
                                         Camera* lightCams, LightState* lights, int NumLights)
{
    m_viewport = viewport;
    
    SceneConstantBuffer sceneConsts = {}; 
    SceneConstantBuffer shadowConsts = {};
    
    // Scale down the world a bit.
    ::XMStoreFloat4x4(&sceneConsts.model, XMMatrixScaling(0.1f, 0.1f, 0.1f));
    ::XMStoreFloat4x4(&shadowConsts.model, XMMatrixScaling(0.1f, 0.1f, 0.1f));

    // The scene pass is drawn from the camera.
    pSceneCamera->Get3DViewProjMatrices(&sceneConsts.view,
        &sceneConsts.projection, 90.0f,
        viewport.Width, viewport.Height);

    // The light pass is drawn from the first light.
    lightCams[0].Get3DViewProjMatrices(&shadowConsts.view,
        &shadowConsts.projection, 90.0f,
        viewport.Width, viewport.Height);

    for (int i = 0; i < NumLights; i++)
    {
        memcpy(&sceneConsts.lights[i], &lights[i], sizeof(LightState));
        memcpy(&shadowConsts.lights[i], &lights[i], sizeof(LightState));
    }

    // The shadow pass won't sample the shadow map, but rather write to it.
    shadowConsts.sampleShadowMap = FALSE;

    // The scene pass samples the shadow map.
    sceneConsts.sampleShadowMap = TRUE;

    shadowConsts.ambientColor = sceneConsts.ambientColor = { 0.1f, 0.2f, 0.3f, 1.0f };

    memcpy(pSceneData, &sceneConsts, sizeof(SceneConstantBuffer));
    memcpy(pShadowData, &shadowConsts, sizeof(SceneConstantBuffer));

    pScreenInfoData->size[0] = std::floor(viewport.Width);
    pScreenInfoData->size[1] = std::floor(viewport.Height);
}

void FrameResource::RenderFrame()
{
    FrameBegin();
    RenderShadow();
    RenderScene();
    RenderPostprocess();
}

void FrameResource::FrameBegin()
{
    // Reset the command allocator and list.
    ThrowIfFailed(commandAllocator->Reset());
    ThrowIfFailed(commandList.Reset());
        
    ID3D12DescriptorHeap* ppHeaps[] = { m_root->m_cbvSrvHeap.Get(), m_samplerHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // Indicate that the back buffer will be used as a render target.
    m_pCurrentFrameResource->m_commandLists[CommandListPre]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the render target and depth stencil.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_pCurrentFrameResource->m_commandLists[CommandListPre]->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_pCurrentFrameResource->m_commandLists[CommandListPre]->ClearDepthStencilView(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void FrameResource::RenderShadow()
{
    PIXBeginEvent(commandList, 0, L"Rendering shadow pass...");
    
    // Shadow pass. We use constant buf #1 and depth stencil #1
    // with rendering to the render target disabled.
    
    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE));
    
    // Clear the depth stencil buffer in preparation for rendering the shadow map.
    commandList->ClearDepthStencilView(m_shadowDepthView, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(0, nullptr,
        FALSE, &m_shadowDepthView);
    commandList->OMSetStencilRef(0);

    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);

    commandList->SetGraphicsRootSignature(m_sigRenderShadow.Get());
    // Set null SRVs for the diffuse/normal textures.
    commandList->SetGraphicsRootDescriptorTable(0, m_root->m_srvNullGPU);    
    commandList->SetGraphicsRootDescriptorTable(1, cbvShadow);
    // Set a null SRV for the shadow texture.
    commandList->SetGraphicsRootDescriptorTable(2, m_nullSrvHandle);      
    commandList->SetGraphicsRootDescriptorTable(3, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
    
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetIndexBuffer(&m_indexBufferView);
    
    for (const SampleAssets::DrawParameters& drawArgs : SampleAssets::Draws)
    {
        commandList->DrawIndexedInstanced(
            drawArgs.IndexCount, 1,
            drawArgs.IndexStart, drawArgs.VertexBase,
            0);
    }

    PIXEndEvent(commandList);
}

void FrameResource::RenderScene()
{
    PIXBeginEvent(commandList, 0, L"Rendering scene pass...");
    // Scene pass. We use constant buf #2 and depth stencil #2
    // with rendering to the render target enabled.
    
    // Transition the shadow map from writeable to readable.
    D3D12_RESOURCE_TRANSITION_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_shadowTexture.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_texSceneColor.Get(),
            D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    
    commandList->ResourceBarrier(_countof(barriers), barriers);

    // Clear the render target and depth stencil.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(m_rtvSceneColorCpu, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(m_dsvDepthStencil, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(1, &m_rtvSceneColorCpu,
        FALSE, &m_dsvDepthStencil);
    commandList->OMSetStencilRef(0);

    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);
    
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetIndexBuffer(&m_indexBufferView);

    commandList->SetGraphicsRootSignature(m_sigRenderScene.Get());
    commandList->SetGraphicsRootDescriptorTable(1, cbvScene);
    commandList->SetGraphicsRootDescriptorTable(2, m_shadowDepthHandle); // Set the shadow texture as an SRV.
    commandList->SetGraphicsRootDescriptorTable(3, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
    
    for (const SampleAssets::DrawParameters& drawArgs : SampleAssets::Draws)
    {
        // Set the diffuse and normal textures for the current object.
        CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_srvTextureStart,
            drawArgs.DiffuseTextureIndex, m_cbvSrvDescriptorSize);
        commandList->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);

        commandList->DrawIndexedInstanced(drawArgs.IndexCount, 1,
            drawArgs.IndexStart, drawArgs.VertexBase, 0);
    }
    
    PIXEndEvent(commandList);
}



