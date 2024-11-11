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
#include "D3D12Tessellation.h"
#include <cmath>

FrameResource::FrameResource(ID3D12Device* pDevice,
    ID3D12Resource* pBackBuffer, const D3D12_CPU_DESCRIPTOR_HANDLE& hBackBuffer,
    UINT frameResourceIndex)
    : fenceValue(0)
    , backBuffer(pBackBuffer)
    , rtvBackBuffer(hBackBuffer)
{
    ThrowIfFailed(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocator)));
    /*ThrowIfFailed(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(), nullptr,
            IID_PPV_ARGS(&commandList)));*/

    NAME_D3D12_OBJECT(commandAllocator);
    //NAME_D3D12_OBJECT(commandList);

    // Close these command lists; don't record into them for now.
    //ThrowIfFailed(commandList->Close());

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
        IID_PPV_ARGS(&cbScene)));

    // Map the constant buffers and cache their heap pointers.
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(cbScene->Map(0, &readRange,
        reinterpret_cast<void**>(&pSceneData)));
}

FrameResource::~FrameResource()
{
    cbScene->Unmap(0, nullptr);
    cbScene.Reset();

    backBuffer = nullptr;
    //commandList.Reset();
    commandAllocator.Reset();
}

// Builds and writes constant buffers from scratch to the proper slots for 
// this frame resource.
void FrameResource::WriteConstantBuffers(const D3D12_VIEWPORT& viewport, Camera* pSceneCamera,
                                         Camera* lightCams, LightState* lights, int NumLights,
                                         float time, float height)
{
    SceneConstantBuffer sceneConsts = {}; 
    
    // The scene pass is drawn from the camera.
    pSceneCamera->Get3DViewProjMatrices(&sceneConsts.view,
        &sceneConsts.projection, 90.0f,
        viewport.Width, viewport.Height);
    
    sceneConsts.screenSize = {viewport.Width, viewport.Height,
        1.0f / viewport.Width, 1.0f / viewport.Height};

    sceneConsts.sceneInfo = {time, height, 0.f, 0.f};
    
    sceneConsts.ambientColor = { 0.1f, 0.1f, 0.1f, 1.0f };
    
    for (int i = 0; i < NumLights; i++)
    {
        memcpy(&sceneConsts.lights[i], &lights[i], sizeof(LightState));
    }
    
    memcpy(pSceneData, &sceneConsts, sizeof(SceneConstantBuffer));
}



