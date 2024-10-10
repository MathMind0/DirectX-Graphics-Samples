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

#include "stdafx.h"
#include "Camera.h"
#include "DXSampleHelper.h"

using namespace DirectX;
using namespace Microsoft::WRL;

enum class FRAME_CSU_DESCRIPTORS : UINT
{
    SHADOW_CBV,
    SCENE_CBV,
    NUM_DESCRIPTORS
};

struct LightState
{
    XMFLOAT4 position;
    XMFLOAT4 direction;
    XMFLOAT4 color;
    XMFLOAT4 falloff;

    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;
};

struct SceneConstantBuffer
{
    XMFLOAT4X4 model;
    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;
    XMFLOAT4 ambientColor;
    BOOL sampleShadowMap;
    BOOL padding[3];        // Must be aligned to be made up of N float4s.
    LightState lights[NumLights];
};

struct ScreenInfo
{
    UINT size[4];
};

struct FrameResource
{
public:    
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    UINT64 fenceValue;

    ID3D12Resource* backBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvBackBuffer;

    ComPtr<ID3D12Resource> cbShadow;
    ComPtr<ID3D12Resource> cbScene;
    SceneConstantBuffer* pShadowData; // WRITE-ONLY pointer to the shadow pass constant buffer.
    SceneConstantBuffer* pSceneData;  // WRITE-ONLY pointer to the scene pass constant buffer.
    D3D12_GPU_DESCRIPTOR_HANDLE cbvShadow;
    D3D12_GPU_DESCRIPTOR_HANDLE cbvScene;

    // Constant Buffer for postprocess
    ComPtr<ID3D12Resource> cbScreenInfo;
    ScreenInfo* pScreenInfoData;
    
public:
    FrameResource(ID3D12Device* pDevice,
        ID3D12DescriptorHeap* pCbvSrvHeap, UINT cbvSrvDescriptorSize,
        ID3D12Resource* pBackBuffer, const D3D12_CPU_DESCRIPTOR_HANDLE& hBackBuffer,
        UINT frameResourceIndex);
    
    ~FrameResource();

    void WriteConstantBuffers(const D3D12_VIEWPORT& viewport, Camera* pSceneCamera,
                              Camera* lightCams, LightState* lights, int NumLights);

private:
};
