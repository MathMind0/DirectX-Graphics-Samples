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

#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

//**********************************************************************************************
//
// RaytracingHLSLCompat.h
//
// A header with shared definitions for C++ and HLSL source files. 
//
//**********************************************************************************************

#ifdef HLSL
#include "util\HlslCompat.h"
#else
using namespace DirectX;

// Shader will use byte encoding to access vertex indices.
typedef UINT16 Index;
#endif

// Number of metaballs to use within an AABB.
#define N_METABALLS 3    // = {3, 5}

// Limitting calculations only to metaballs a ray intersects can speed up raytracing
// dramatically particularly when there is a higher number of metaballs used. 
// Use of dynamic loops can have detrimental effects to performance for low iteration counts
// and outweighing any potential gains from avoiding redundant calculations.
// Requires: USE_DYNAMIC_LOOPS set to 1 to take effect.
#if N_METABALLS >= 5
#define USE_DYNAMIC_LOOPS 1
#define LIMIT_TO_ACTIVE_METABALLS 1
#else 
#define USE_DYNAMIC_LOOPS 0
#define LIMIT_TO_ACTIVE_METABALLS 0
#endif

#define N_FRACTAL_ITERATIONS 4      // = <1,...>

// PERFORMANCE TIP: Set max recursion depth as low as needed
// as drivers may apply optimization strategies for low recursion depths.
#define MAX_RAY_RECURSION_DEPTH 3    // ~ primary rays + reflections + shadow rays from reflected geometry.


struct ProceduralPrimitiveAttributes
{
    XMFLOAT3 normal;
};

struct RayPayload
{
    XMFLOAT4 color;
    UINT   recursionDepth;
};

struct ShadowRayPayload
{
    bool hit;
};

struct SceneConstantBuffer
{
    XMMATRIX projectionToWorld;
    XMVECTOR cameraPosition;
    XMVECTOR lightPosition;
    XMVECTOR lightAmbientColor;
    XMVECTOR lightDiffuseColor;
    float    reflectance;
    float    elapsedTime;                 // Elapsed application time.
};

// Attributes per primitive type.
struct PrimitiveConstantBuffer
{
    XMFLOAT4 albedo;
    float reflectanceCoef;
    float diffuseCoef;
    float specularCoef;
    float specularPower;
    float stepScale;                      // Step scale for ray marching of signed distance primitives. 
                                          // - Some object transformations don't preserve the distances and 
                                          //   thus require shorter steps.
    XMFLOAT3 padding;
};

// Attributes per primitive instance.
struct PrimitiveInstanceConstantBuffer
{
    UINT instanceIndex;  
    UINT primitiveType; // Procedural primitive type
};

// Dynamic attributes per primitive instance.
struct PrimitiveInstancePerFrameBuffer
{
    XMMATRIX localSpaceToBottomLevelAS;   // Matrix from local primitive space to bottom-level object space.
    XMMATRIX bottomLevelASToLocalSpace;   // Matrix from bottom-level object space to local primitive space.
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
};


// Ray types traced in this sample.
namespace RayType {
    enum Enum {
        Radiance = 0,   // ~ Primary, reflected camera/view rays calculating color for each hit.
        Shadow,         // ~ Shadow/visibility rays, only testing for occlusion
        Count
    };
}

namespace TraceRayParameters
{
    static const UINT InstanceMask = ~0;   // Everything is visible.
    namespace HitGroup {
        static const UINT Offset[RayType::Count] =
        {
            0, // Radiance ray
            1  // Shadow ray
        };
        static const UINT GeometryStride = RayType::Count;
    }
    namespace MissShader {
        static const UINT Offset[RayType::Count] =
        {
            0, // Radiance ray
            1  // Shadow ray
        };
    }
}

// From: http://blog.selfshadow.com/publications/s2015-shading-course/hoffman/s2015_pbs_physics_math_slides.pdf
static const XMFLOAT4 TitaniumReflectance = XMFLOAT4(0.542f, 0.497f, 0.449f, 1.0f);
static const XMFLOAT4 ChromiumReflectance = XMFLOAT4(0.549f, 0.556f, 0.554f, 1.0f);
static const XMFLOAT4 IronReflectance = XMFLOAT4(0.562f, 0.565f, 0.578f, 1.0f);
static const XMFLOAT4 NickelReflectance = XMFLOAT4(0.660f, 0.609f, 0.526f, 1.0f);
static const XMFLOAT4 PlatinumReflectance = XMFLOAT4(0.673f, 0.637f, 0.585f, 1.0f);
static const XMFLOAT4 CopperReflectance = XMFLOAT4(0.955f, 0.638f, 0.538f, 1.0f);
static const XMFLOAT4 PalladiumReflectance = XMFLOAT4(0.733f, 0.697f, 0.652f, 1.0f);
static const XMFLOAT4 ZincReflectance = XMFLOAT4(0.664f, 0.824f, 0.850f, 1.0f);
static const XMFLOAT4 GoldReflectance = XMFLOAT4(1.022f, 0.782f, 0.344f, 1.0f);
static const XMFLOAT4 AluminumReflectance = XMFLOAT4(0.913f, 0.922f, 0.924f, 1.0f);
static const XMFLOAT4 SilverReflectance = XMFLOAT4(0.972f, 0.960f, 0.915f, 1.0f);

static const XMFLOAT4 BackgroundColor = XMFLOAT4(0.7f, 0.7f, 1.0f, 1.0f);
static const float InShadowRadiance = 0.35f;

static const XMFLOAT4 green = XMFLOAT4(0.1f, 1.0f, 0.5f, 1.0f);
static const XMFLOAT4 red = XMFLOAT4(1.0f, 0.5f, 0.5f, 1.0f);
static const XMFLOAT4 yellow = XMFLOAT4(1.0f, 1.0f, 0.5f, 1.0f);

namespace AnalyticPrimitive {
    enum Enum {
        AABB = 0,
        Spheres,
        Count
    };
}

namespace VolumetricPrimitive {
    enum Enum {
        Metaballs = 0,
        Count
    };
}

namespace SignedDistancePrimitive {
    enum Enum {
        MiniSpheres = 0,
        IntersectedRoundCube,
        SquareTorus,
        TwistedTorus,
        Cog,
        Cylinder,
        FractalPyramid,
        Count
    };
}

#endif // RAYTRACINGHLSLCOMPAT_H