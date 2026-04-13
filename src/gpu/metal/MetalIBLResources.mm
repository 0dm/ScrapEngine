#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <gpu/metal/MetalIBLResources.hpp>

#include <stb_image.h>

#include <cstring>
#include <memory>
#include <string>

namespace scrap::gpu::metal {

    namespace {

        constexpr const char* kIblMsl = R"msl(
#include <metal_stdlib>
using namespace metal;

constant float PI = 3.14159265359f;

float3 uvToDir(uint face, float2 uv) {
  float u = uv.x * 2.0f - 1.0f;
  float v = uv.y * 2.0f - 1.0f;
  switch (face) {
    case 0: return normalize(float3( 1.0f,   -v,   -u));
    case 1: return normalize(float3(-1.0f,   -v,    u));
    case 2: return normalize(float3(   u,  1.0f,    v));
    case 3: return normalize(float3(   u, -1.0f,   -v));
    case 4: return normalize(float3(   u,   -v,  1.0f));
    case 5: return normalize(float3(  -u,   -v, -1.0f));
    default: return float3(0, 0, 1);
  }
}

float2 dirToEquirectUV(float3 dir) {
  float phi = atan2(dir.z, -dir.x);
  float theta = asin(clamp(dir.y, -1.0f, 1.0f));
  return float2(phi / (2.0f * PI) + 0.5f, theta / PI + 0.5f);
}

float3 sampleCubemapArray(texture2d_array<float, access::sample> tex, sampler s, float3 rd, uint mip) {
  float3 a = abs(rd);
  float ma = max(max(a.x, a.y), a.z);
  float2 st;
  uint face;
  if (a.x >= a.y && a.x >= a.z) {
    if (rd.x > 0) { face = 0; st = float2(-rd.z, -rd.y) / ma * 0.5f + 0.5f; }
    else { face = 1; st = float2( rd.z, -rd.y) / ma * 0.5f + 0.5f; }
  } else if (a.y >= a.z) {
    if (rd.y > 0) { face = 2; st = float2( rd.x,  rd.z) / ma * 0.5f + 0.5f; }
    else { face = 3; st = float2( rd.x, -rd.z) / ma * 0.5f + 0.5f; }
  } else {
    if (rd.z > 0) { face = 4; st = float2( rd.x, -rd.y) / ma * 0.5f + 0.5f; }
    else { face = 5; st = float2(-rd.x, -rd.y) / ma * 0.5f + 0.5f; }
  }
  return tex.sample(s, st, face, level(mip)).rgb;
}

float radicalInverse(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}

float2 hammersley(uint i, uint N) {
  return float2(float(i) / float(N), radicalInverse(i));
}

float3 importanceSampleGGX(float2 Xi, float3 N, float roughness) {
  float a = roughness * roughness;
  float phi = 2.0f * PI * Xi.x;
  float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
  float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
  float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
  float3 up = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
  float3 tangent = normalize(cross(up, N));
  float3 bitangent = cross(N, tangent);
  return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

kernel void ibl_equirect_to_cube(
    texture2d<float, access::sample> equirect [[texture(0)]],
    texture2d_array<float, access::write> outCube [[texture(1)]],
    sampler smp [[sampler(0)]],
    constant uint& faceSize [[buffer(0)]],
    uint3 gid [[thread_position_in_grid]]) {
  uint face = gid.z;
  if (gid.x >= faceSize || gid.y >= faceSize || face >= 6) return;
  float2 uv = (float2(gid.xy) + 0.5f) / float(faceSize);
  float3 dir = uvToDir(face, uv);
  float2 eu = dirToEquirectUV(dir);
  float3 c = equirect.sample(smp, eu).rgb;
  outCube.write(float4(c, 1.0f), gid.xy, face, 0);
}

kernel void ibl_irradiance_fixed(
    texture2d_array<float, access::sample> envMap [[texture(0)]],
    sampler envSmp [[sampler(0)]],
    texture2d_array<float, access::write> outputIrr [[texture(1)]],
    constant uint& faceSize [[buffer(0)]],
    uint3 gid [[thread_position_in_grid]]) {
  uint face = gid.z;
  if (gid.x >= faceSize || gid.y >= faceSize || face >= 6) return;
  float2 uv = (float2(gid.xy) + 0.5f) / float(faceSize);
  float3 N = uvToDir(face, uv);
  float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(0, 0, 1);
  float3 right = normalize(cross(up, N));
  up = cross(N, right);
  float3 irradiance = float3(0);
  float sampleCount = 0;
  float sampleDelta = 0.025f;
  for (float phi = 0; phi < 2.0f * PI; phi += sampleDelta) {
    for (float theta = 0; theta < 0.5f * PI; theta += sampleDelta) {
      float cosTheta = cos(theta);
      float sinTheta = sin(theta);
      float3 tangentSample = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
      float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
      float3 c = sampleCubemapArray(envMap, envSmp, sampleVec, 0);
      irradiance += c * cosTheta * sinTheta;
      sampleCount += 1.0f;
    }
  }
  irradiance = PI * irradiance / max(sampleCount, 1.0f);
  outputIrr.write(float4(irradiance, 1.0f), gid.xy, face, 0);
}

struct PrefilterPC {
  uint faceSize;
  float roughness;
  uint mipLevel;
  uint _pad;
};

kernel void ibl_prefilter(
    texture2d_array<float, access::sample> envMap [[texture(0)]],
    sampler envSmp [[sampler(0)]],
    texture2d_array<float, access::write> outputPref [[texture(1)]],
    constant PrefilterPC& pc [[buffer(0)]],
    uint3 gid [[thread_position_in_grid]]) {
  uint face = gid.z;
  if (gid.x >= pc.faceSize || gid.y >= pc.faceSize || face >= 6) return;
  float2 uv = (float2(gid.xy) + 0.5f) / float(pc.faceSize);
  float3 N = uvToDir(face, uv);
  float3 V = N;
  const uint SAMPLE_COUNT = 1024u;
  float totalWeight = 0;
  float3 prefiltered = float3(0);
  for (uint i = 0; i < SAMPLE_COUNT; ++i) {
    float2 Xi = hammersley(i, SAMPLE_COUNT);
    float3 H = importanceSampleGGX(Xi, N, pc.roughness);
    float3 L = normalize(2.0f * dot(V, H) * H - V);
    float NdotL = max(dot(N, L), 0.0f);
    if (NdotL > 0.0f) {
      prefiltered += sampleCubemapArray(envMap, envSmp, L, 0) * NdotL;
      totalWeight += NdotL;
    }
  }
  prefiltered /= max(totalWeight, 0.001f);
  outputPref.write(float4(prefiltered, 1.0f), gid.xy, face, pc.mipLevel);
}

float geometrySchlickGGX(float NdotV, float roughness) {
  float a = roughness;
  float k = (a * a) / 2.0f;
  return NdotV / (NdotV * (1.0f - k) + k);
}

float geometrySmith(float3 N, float3 V, float3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0f);
  float NdotL = max(dot(N, L), 0.0f);
  return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

float2 integrateBRDF(float NdotV, float roughness) {
  float3 V = float3(sqrt(max(1.0f - NdotV * NdotV, 0.0f)), 0, NdotV);
  float A = 0, B = 0;
  float3 N = float3(0, 0, 1);
  const uint SAMPLE_COUNT = 1024u;
  for (uint i = 0; i < SAMPLE_COUNT; ++i) {
    float2 Xi = hammersley(i, SAMPLE_COUNT);
    float3 H = importanceSampleGGX(Xi, N, roughness);
    float3 L = normalize(2.0f * dot(V, H) * H - V);
    float NdotL = max(L.z, 0.0f);
    float NdotH = max(H.z, 0.0f);
    float VdotH = max(dot(V, H), 0.0f);
    if (NdotL > 0.0f) {
      float G = geometrySmith(N, V, L, roughness);
      float G_Vis = (G * VdotH) / (NdotH * NdotV + 1e-4f);
      float Fc = pow(1.0f - VdotH, 5.0f);
      A += (1.0f - Fc) * G_Vis;
      B += Fc * G_Vis;
    }
  }
  return float2(A, B) / float(SAMPLE_COUNT);
}

struct BrdfPC { uint width; uint height; };

kernel void ibl_brdf_lut(
    texture2d<float, access::write> outputLUT [[texture(0)]],
    constant BrdfPC& pc [[buffer(0)]],
    uint3 gid [[thread_position_in_grid]]) {
  if (gid.x >= pc.width || gid.y >= pc.height) return;
  float NdotV = (float(gid.x) + 0.5f) / float(pc.width);
  float roughness = (float(gid.y) + 0.5f) / float(pc.height);
  NdotV = max(NdotV, 0.001f);
  roughness = max(roughness, 0.001f);
  outputLUT.write(float4(integrateBRDF(NdotV, roughness), 0, 1), gid.xy);
}
)msl";

    } // namespace

    void releaseMetalIBLMaps(MetalIBLMaps& maps) {
        if (maps.envMapArray) {
            CFBridgingRelease(maps.envMapArray);
            maps.envMapArray = nullptr;
        }
        if (maps.irradianceArray) {
            CFBridgingRelease(maps.irradianceArray);
            maps.irradianceArray = nullptr;
        }
        if (maps.prefilterArray) {
            CFBridgingRelease(maps.prefilterArray);
            maps.prefilterArray = nullptr;
        }
        if (maps.brdfLUT) {
            CFBridgingRelease(maps.brdfLUT);
            maps.brdfLUT = nullptr;
        }
        if (maps.samplerState) {
            CFBridgingRelease(maps.samplerState);
            maps.samplerState = nullptr;
        }
    }

    void deleteMetalIBLMaps(MetalIBLMaps* maps) {
        if (maps) {
            releaseMetalIBLMaps(*maps);
            delete maps;
        }
    }

    MetalIBLMapsPtr generateMetalIBLMaps(void* mtlDevicePtr, const std::string& hdrPath) {
        id<MTLDevice> device = (__bridge id<MTLDevice>)mtlDevicePtr;
        if (device == nil) {
            return MetalIBLMapsPtr(nullptr, deleteMetalIBLMaps);
        }

        int w = 0, h = 0, ch = 0;
        float* pixels = stbi_loadf(hdrPath.c_str(), &w, &h, &ch, 4);
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels)
                stbi_image_free(pixels);
            return MetalIBLMapsPtr(nullptr, deleteMetalIBLMaps);
        }

        NSError* err = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kIblMsl]
                                                  options:nil
                                                    error:&err];
        if (lib == nil) {
            stbi_image_free(pixels);
            return MetalIBLMapsPtr(nullptr, deleteMetalIBLMaps);
        }

        id<MTLFunction> fnEq = [lib newFunctionWithName:@"ibl_equirect_to_cube"];
        id<MTLFunction> fnIrr = [lib newFunctionWithName:@"ibl_irradiance_fixed"];
        id<MTLFunction> fnPre = [lib newFunctionWithName:@"ibl_prefilter"];
        id<MTLFunction> fnBrdf = [lib newFunctionWithName:@"ibl_brdf_lut"];
        if (!fnEq || !fnIrr || !fnPre || !fnBrdf) {
            stbi_image_free(pixels);
            return MetalIBLMapsPtr(nullptr, deleteMetalIBLMaps);
        }

        NSError* pe = nil;
        id<MTLComputePipelineState> psEq = [device newComputePipelineStateWithFunction:fnEq
                                                                                 error:&pe];
        id<MTLComputePipelineState> psIrr = [device newComputePipelineStateWithFunction:fnIrr
                                                                                  error:&pe];
        id<MTLComputePipelineState> psPre = [device newComputePipelineStateWithFunction:fnPre
                                                                                  error:&pe];
        id<MTLComputePipelineState> psBrdf = [device newComputePipelineStateWithFunction:fnBrdf
                                                                                   error:&pe];
        if (!psEq || !psIrr || !psPre || !psBrdf) {
            stbi_image_free(pixels);
            return MetalIBLMapsPtr(nullptr, deleteMetalIBLMaps);
        }

        MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
        sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.mipFilter = MTLSamplerMipFilterLinear;
        sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        sampDesc.maxAnisotropy = 1;
        id<MTLSamplerState> smp = [device newSamplerStateWithDescriptor:sampDesc];

        MTLTextureDescriptor* tdEqIn =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                               width:static_cast<NSUInteger>(w)
                                                              height:static_cast<NSUInteger>(h)
                                                           mipmapped:NO];
        tdEqIn.usage = MTLTextureUsageShaderRead;
        tdEqIn.storageMode = MTLStorageModeShared;
        id<MTLTexture> texEqIn = [device newTextureWithDescriptor:tdEqIn];
        [texEqIn replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(w),
                                               static_cast<NSUInteger>(h))
                   mipmapLevel:0
                     withBytes:pixels
                   bytesPerRow:static_cast<NSUInteger>(w) * 16];
        stbi_image_free(pixels);

        const NSUInteger envRes = 512;
        MTLTextureDescriptor* tdEnv =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                               width:envRes
                                                              height:envRes
                                                           mipmapped:NO];
        tdEnv.textureType = MTLTextureType2DArray;
        tdEnv.arrayLength = 6;
        tdEnv.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        tdEnv.storageMode = MTLStorageModePrivate;
        id<MTLTexture> texEnv = [device newTextureWithDescriptor:tdEnv];

        const NSUInteger irrRes = 32;
        MTLTextureDescriptor* tdIrr =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                               width:irrRes
                                                              height:irrRes
                                                           mipmapped:NO];
        tdIrr.textureType = MTLTextureType2DArray;
        tdIrr.arrayLength = 6;
        tdIrr.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        tdIrr.storageMode = MTLStorageModePrivate;
        id<MTLTexture> texIrr = [device newTextureWithDescriptor:tdIrr];

        const NSUInteger prefBase = 128;
        const NSUInteger prefMips = 5;
        MTLTextureDescriptor* tdPref =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                               width:prefBase
                                                              height:prefBase
                                                           mipmapped:YES];
        tdPref.textureType = MTLTextureType2DArray;
        tdPref.arrayLength = 6;
        tdPref.mipmapLevelCount = prefMips;
        tdPref.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        tdPref.storageMode = MTLStorageModePrivate;
        id<MTLTexture> texPref = [device newTextureWithDescriptor:tdPref];

        const NSUInteger brdfRes = 512;
        MTLTextureDescriptor* tdBrdf =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                               width:brdfRes
                                                              height:brdfRes
                                                           mipmapped:NO];
        tdBrdf.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        tdBrdf.storageMode = MTLStorageModePrivate;
        id<MTLTexture> texBrdf = [device newTextureWithDescriptor:tdBrdf];

        id<MTLCommandQueue> queue = [device newCommandQueue];
        auto submit = [&](void (^encode)(id<MTLComputeCommandEncoder>)) {
            id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
            encode(enc);
            [enc endEncoding];
            [cmdBuf commit];
            [cmdBuf waitUntilCompleted];
        };

        submit(^(id<MTLComputeCommandEncoder> enc) {
          [enc setComputePipelineState:psEq];
          [enc setTexture:texEqIn atIndex:0];
          [enc setTexture:texEnv atIndex:1];
          [enc setSamplerState:smp atIndex:0];
          uint32_t fs = static_cast<uint32_t>(envRes);
          [enc setBytes:&fs length:sizeof(fs) atIndex:0];
          MTLSize thr = MTLSizeMake(8, 8, 1);
          MTLSize grid = MTLSizeMake((envRes + 7) / 8, (envRes + 7) / 8, 6);
          [enc dispatchThreadgroups:grid threadsPerThreadgroup:thr];
        });

        submit(^(id<MTLComputeCommandEncoder> enc) {
          [enc setComputePipelineState:psIrr];
          [enc setTexture:texEnv atIndex:0];
          [enc setSamplerState:smp atIndex:0];
          [enc setTexture:texIrr atIndex:1];
          uint32_t fs = static_cast<uint32_t>(irrRes);
          [enc setBytes:&fs length:sizeof(fs) atIndex:0];
          MTLSize thr = MTLSizeMake(8, 8, 1);
          MTLSize grid = MTLSizeMake((irrRes + 7) / 8, (irrRes + 7) / 8, 6);
          [enc dispatchThreadgroups:grid threadsPerThreadgroup:thr];
        });

        for (NSUInteger mip = 0; mip < prefMips; ++mip) {
            NSUInteger mipRes = prefBase >> mip;
            float roughness =
                (prefMips <= 1) ? 0.f : static_cast<float>(mip) / static_cast<float>(prefMips - 1);
            struct {
                uint32_t faceSize;
                float roughness;
                uint32_t mipLevel;
                uint32_t _pad;
            } pc{static_cast<uint32_t>(mipRes), roughness, static_cast<uint32_t>(mip), 0};
            submit(^(id<MTLComputeCommandEncoder> enc) {
              [enc setComputePipelineState:psPre];
              [enc setTexture:texEnv atIndex:0];
              [enc setSamplerState:smp atIndex:0];
              [enc setTexture:texPref atIndex:1];
              [enc setBytes:&pc length:sizeof(pc) atIndex:0];
              MTLSize thr = MTLSizeMake(8, 8, 1);
              MTLSize grid = MTLSizeMake((mipRes + 7) / 8, (mipRes + 7) / 8, 6);
              [enc dispatchThreadgroups:grid threadsPerThreadgroup:thr];
            });
        }

        submit(^(id<MTLComputeCommandEncoder> enc) {
          [enc setComputePipelineState:psBrdf];
          [enc setTexture:texBrdf atIndex:0];
          struct {
              uint32_t width;
              uint32_t height;
          } bc{static_cast<uint32_t>(brdfRes), static_cast<uint32_t>(brdfRes)};
          [enc setBytes:&bc length:sizeof(bc) atIndex:0];
          MTLSize thr = MTLSizeMake(16, 16, 1);
          MTLSize grid = MTLSizeMake((brdfRes + 15) / 16, (brdfRes + 15) / 16, 1);
          [enc dispatchThreadgroups:grid threadsPerThreadgroup:thr];
        });

        auto* out = new MetalIBLMaps();
        out->envMapArray = (__bridge_retained void*)texEnv;
        out->irradianceArray = (__bridge_retained void*)texIrr;
        out->prefilterArray = (__bridge_retained void*)texPref;
        out->brdfLUT = (__bridge_retained void*)texBrdf;
        out->samplerState = (__bridge_retained void*)smp;

        return MetalIBLMapsPtr(out, deleteMetalIBLMaps);
    }

} // namespace scrap::gpu::metal
