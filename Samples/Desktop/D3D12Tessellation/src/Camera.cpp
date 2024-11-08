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
#include "Camera.h"

Camera::Camera()
{
    Reset();
}

Camera::~Camera()
{
}

void Camera::Get3DViewProjMatrices(XMFLOAT4X4 *view, XMFLOAT4X4 *proj, float fovInDegrees,
    float screenWidth, float screenHeight) const
{
    
    float aspectRatio = (float)screenWidth / (float)screenHeight;
    float fovAngleY = fovInDegrees * XM_PI / 180.0f;

    if (aspectRatio < 1.0f)
    {
        fovAngleY /= aspectRatio;
    }

    XMStoreFloat4x4(view, XMMatrixTranspose(XMMatrixLookToRH(mEye, mLookTo, mUp)));
    XMStoreFloat4x4(proj, XMMatrixTranspose(XMMatrixPerspectiveFovRH(fovAngleY, aspectRatio, 0.1f, 5000.0f)));
}

void Camera::GetOrthoProjMatrices(XMFLOAT4X4 *view, XMFLOAT4X4 *proj,
    float width, float height) const
{
    XMStoreFloat4x4(view, XMMatrixTranspose(XMMatrixLookToRH(mEye, mLookTo, mUp)));
    XMStoreFloat4x4(proj, XMMatrixTranspose(XMMatrixOrthographicRH(width, height, 0.01f, 125.0f)));
}

void Camera::RotateYaw(float deg)
{
    XMMATRIX rotation = XMMatrixRotationAxis(mUp, deg);

    mLookTo = XMVector3Transform(mLookTo, rotation);
    mRight = XMVector3Cross(mLookTo, mUp);
    mRight = XMVector3Normalize(mRight);
}

void Camera::RotatePitch(float deg)
{
    XMMATRIX rotation = XMMatrixRotationAxis(mRight, deg);

    mLookTo = XMVector3Transform(mLookTo, rotation);
}

void Camera::Reset()
{
    mEye = XMVectorSet(0.0f, 100.0f, 100.0f, 0.0f);
    mLookTo = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
    mUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    mRight = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
}

void Camera::Set(XMVECTOR eye, XMVECTOR lookTo)
{
    mEye = eye;
    mLookTo = XMVector3Normalize(lookTo);

    mRight = XMVector3Cross(mLookTo, mUp);
    mRight = XMVector3Normalize(mRight);
}

void Camera::MoveForward(float distance)
{
    mEye += distance * mLookTo;
}

void Camera::Strafe(float distance)
{
    mEye += distance * mRight;
}

void Camera::Elevate(float distance)
{
    mEye += distance * mUp;
}
