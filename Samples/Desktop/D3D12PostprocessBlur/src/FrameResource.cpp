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
#include "D3D12PostprocessBlur.h"
#include "SquidRoom.h"
#include <cmath>

FrameResource::FrameResource(ID3D12Device* pDevice,
    ID3D12DescriptorHeap* pCbvSrvHeap, UINT cbvSrvDescriptorSize,
    ID3D12Resource* pBackBuffer, const D3D12_CPU_DESCRIPTOR_HANDLE& hBackBuffer,
    UINT frameResourceIndex)
    : fenceValue(0)
    , backBuffer(pBackBuffer)
    , rtvBackBuffer(hBackBuffer)
{
    ThrowIfFailed(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocator)));
    ThrowIfFailed(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(), nullptr,
            IID_PPV_ARGS(&commandList)));

    NAME_D3D12_OBJECT(commandAllocator);
    NAME_D3D12_OBJECT(commandList);

    // Close these command lists; don't record into them for now.
    ThrowIfFailed(commandList->Close());

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
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleShadowCB(
        pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)FRAME_CSU_DESCRIPTORS::NUM_DESCRIPTORS * frameResourceIndex +
        (INT)FRAME_CSU_DESCRIPTORS::SHADOW_CBV + (INT)CSU_DESCRIPTORS::NUM_DESCRIPTORS,
        cbvSrvDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandleShadowCB(
        pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)FRAME_CSU_DESCRIPTORS::NUM_DESCRIPTORS * frameResourceIndex +
        (INT)FRAME_CSU_DESCRIPTORS::SHADOW_CBV + (INT)CSU_DESCRIPTORS::NUM_DESCRIPTORS,
        cbvSrvDescriptorSize);
    
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.SizeInBytes = constantBufferSize;

    // Describe and create the shadow constant buffer view (CBV) and 
    // cache the GPU descriptor handle.
    cbvDesc.BufferLocation = cbShadow->GetGPUVirtualAddress();
    pDevice->CreateConstantBufferView(&cbvDesc, cpuHandleShadowCB);
    cbvShadow = gpuHandleShadowCB;

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleSceneCB(
    pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
    (INT)FRAME_CSU_DESCRIPTORS::NUM_DESCRIPTORS * frameResourceIndex +
    (INT)FRAME_CSU_DESCRIPTORS::SCENE_CBV + (INT)CSU_DESCRIPTORS::NUM_DESCRIPTORS,
    cbvSrvDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandleSceneCB(
        pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)FRAME_CSU_DESCRIPTORS::NUM_DESCRIPTORS * frameResourceIndex +
        (INT)FRAME_CSU_DESCRIPTORS::SCENE_CBV + (INT)CSU_DESCRIPTORS::NUM_DESCRIPTORS,
        cbvSrvDescriptorSize);
    
    // Describe and create the scene constant buffer view (CBV) and 
    // cache the GPU descriptor handle.
    cbvDesc.BufferLocation = cbScene->GetGPUVirtualAddress();
    pDevice->CreateConstantBufferView(&cbvDesc, cpuHandleSceneCB);
    cbvScene = gpuHandleSceneCB;
    
    const UINT szScreenInfoCB = (sizeof(ScreenInfo) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) &
        ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(szScreenInfoCB),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&cbScreenInfo)));

    ThrowIfFailed(cbScreenInfo->Map(0, &readRange,
        reinterpret_cast<void**>(&pScreenInfoData)));
}

FrameResource::~FrameResource()
{
    cbShadow->Unmap(0, nullptr);
    cbShadow.Reset();
    
    cbScene->Unmap(0, nullptr);
    cbScene.Reset();
    
    cbScreenInfo->Unmap(0, nullptr);
    cbScreenInfo.Reset();

    backBuffer = nullptr;
    commandList.Reset();
    commandAllocator.Reset();
}

// Builds and writes constant buffers from scratch to the proper slots for 
// this frame resource.
void FrameResource::WriteConstantBuffers(const D3D12_VIEWPORT& viewport, Camera* pSceneCamera,
                                         Camera* lightCams, LightState* lights, int NumLights)
{
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

    pScreenInfoData->size[0] = static_cast<UINT>(std::floor(viewport.Width));
    pScreenInfoData->size[1] = static_cast<UINT>(std::floor(viewport.Height));
}



