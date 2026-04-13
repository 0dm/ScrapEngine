#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <gpu/metal/MetalPBRRenderer.hpp>

#include <app/Log.hpp>
#include <app/components/LightComponent.hpp>
#include <app/modeling/Material.hpp>
#include <app/modeling/Texture.hpp>

#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace scrap::gpu::metal {

    namespace {

        constexpr std::size_t kMaxLights = 64;

        constexpr const char* kPbrMsl = R"msl(
#include <metal_stdlib>
using namespace metal;

constant float PI = 3.14159265359f;

float3 sampleCubemapArray(texture2d_array<float, access::sample> tex, sampler s, float3 rd, float mip) {
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

struct SkyVIn { float3 pos [[attribute(0)]]; };
struct SkyFrame {
  float4x4 view;
  float4x4 proj;
};
struct SkyVOut {
  float3 coord;
  float4 position [[position]];
};

vertex SkyVOut sky_vs(SkyVIn in [[stage_in]], constant SkyFrame& fr [[buffer(1)]]) {
  SkyVOut o;
  o.coord = float3(in.pos.x, -in.pos.z, in.pos.y);
  float3x3 viewRot = float3x3(fr.view[0].xyz, fr.view[1].xyz, fr.view[2].xyz);
  float3 pvs = viewRot * in.pos;
  float4 clip = fr.proj * float4(pvs, 1.0f);
  o.position = clip.xyww;
  return o;
}

fragment float4 sky_fs(SkyVOut in [[stage_in]],
    texture2d_array<float, access::sample> env [[texture(0)]],
    sampler smp [[sampler(0)]]) {
  float3 c = sampleCubemapArray(env, smp, normalize(in.coord), 0.0f);
  float3 m = c / (c + 1.0f);
  return float4(m, 1.0f);
}

struct VIn {
  float3 inPosition [[attribute(0)]];
  float3 inNormal [[attribute(1)]];
  float2 inTexCoord [[attribute(2)]];
  float3 inColour [[attribute(3)]];
  float4 inTangent [[attribute(4)]];
};
struct FrameUBO {
  float4x4 view;
  float4x4 proj;
  float4 cameraPos;
};
struct DrawPush {
  float4x4 model;
  uint lightCount;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};
struct MaterialData {
  float4 baseColorFactor;
  float metallicFactor;
  float roughnessFactor;
  float normalScale;
  float occlusionStrength;
  float3 emissiveFactor;
  float alphaCutoff;
  uint alphaMode; // 0=Opaque, 1=Mask, 2=Blend (Blend needs a separate pipeline for correct sorting)
  uint _padA0;
  uint _padA1;
  uint _padA2;
};
struct Light {
  float3 position;
  uint type;
  float3 direction;
  float intensity;
  float3 color;
  float range;
  float innerConeAngle;
  float outerConeAngle;
  float2 _lpad;
};
struct VOutP {
  float4 position [[position]];
  float3 worldPos;
  float3 normal;
  float2 texCoord;
  float4 tangent;
  float3 colour;
  float3 cameraPos [[flat]];
  uint lightCount [[flat]];
};

float3x3 constructTBN(float3 N, float4 T) {
  float3 normal = normalize(N);
  float3 tangent = normalize(T.xyz);
  tangent = normalize(tangent - dot(tangent, normal) * normal);
  float3 bitangent = cross(normal, tangent) * T.w;
  return float3x3(tangent, bitangent, normal);
}

float3x3 normalMatrixFromModel(float4x4 model) {
  // Engine scene transforms are TRS without shear. For M = R*S, the normal matrix is
  // R*inverse(S), which is equivalent to dividing each basis column by its squared length.
  float3 c0 = model[0].xyz;
  float3 c1 = model[1].xyz;
  float3 c2 = model[2].xyz;
  float3 n0 = c0 / max(dot(c0, c0), 1e-8f);
  float3 n1 = c1 / max(dot(c1, c1), 1e-8f);
  float3 n2 = c2 / max(dot(c2, c2), 1e-8f);
  return float3x3(n0, n1, n2);
}

float3 getNormalFromMap(float3x3 tbn, texture2d<half, access::sample> nm, sampler smp, float2 uv, float nScale) {
  float3 n = float3(nm.sample(smp, uv).rgb) * 2.0f - 1.0f;
  n.xy *= nScale;
  n = normalize(n);
  return normalize(tbn * n);
}
float DistributionGGX(float3 N, float3 H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0f);
  float NdotH2 = NdotH * NdotH;
  float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
  denom = PI * denom * denom;
  return a2 / max(denom, 1e-7f);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0f;
  float k = (r * r) / 8.0f;
  return NdotV / (NdotV * (1.0f - k) + k + 1e-7f);
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0f);
  float NdotL = max(dot(N, L), 0.0f);
  return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}
float3 fresnelSchlick(float cosTheta, float3 F0) {
  return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

struct LC { float3 L; float3 radiance; };
LC computeLight(Light light, float3 worldPos) {
  LC r; r.L = float3(0); r.radiance = float3(0);
  if (light.type == 0u) {
    r.L = normalize(-light.direction);
    r.radiance = light.color * light.intensity;
  } else if (light.type == 1u) {
    float3 toLight = light.position - worldPos;
    float dist = length(toLight);
    r.L = toLight / max(dist, 1e-6f);
    float atten = 1.0f / (dist * dist + 1e-4f);
    if (light.range > 0.0f) {
      float ratio = dist / light.range;
      float f = saturate(1.0f - ratio * ratio * ratio * ratio);
      atten *= f * f;
    }
    r.radiance = light.color * light.intensity * atten;
  } else {
    float3 toLight = light.position - worldPos;
    float dist = length(toLight);
    r.L = toLight / max(dist, 1e-6f);
    float atten = 1.0f / (dist * dist + 1e-4f);
    float theta = dot(r.L, normalize(-light.direction));
    float ci = cos(light.innerConeAngle);
    float co = cos(light.outerConeAngle);
    float spot = smoothstep(co, ci, theta);
    if (light.range > 0.0f) {
      float ratio = dist / light.range;
      float f = saturate(1.0f - ratio * ratio * ratio * ratio);
      atten *= f * f;
    }
    r.radiance = light.color * light.intensity * atten * spot;
  }
  return r;
}

vertex VOutP pbr_vs(VIn in [[stage_in]], constant FrameUBO& ubo [[buffer(1)]], constant DrawPush& push [[buffer(2)]]) {
  VOutP o;
  float4 wp = push.model * float4(in.inPosition, 1.0f);
  o.worldPos = wp.xyz;
  o.position = ubo.proj * (ubo.view * wp);
  float3x3 nm = normalMatrixFromModel(push.model);
  o.normal = normalize(nm * in.inNormal);
  o.tangent = float4(normalize(nm * in.inTangent.xyz), in.inTangent.w);
  o.texCoord = in.inTexCoord;
  o.colour = in.inColour;
  o.cameraPos = ubo.cameraPos.xyz;
  o.lightCount = push.lightCount;
  return o;
}

fragment float4 pbr_fs(VOutP v [[stage_in]],
    constant MaterialData& mat [[buffer(4)]],
    constant Light* lights [[buffer(3)]],
    texture2d_array<float, access::sample> irradiance [[texture(0)]],
    texture2d_array<float, access::sample> prefilter [[texture(1)]],
    texture2d<float, access::sample> brdfLUT [[texture(2)]],
    texture2d<half, access::sample> baseColorMap [[texture(3)]],
    texture2d<half, access::sample> normalMap [[texture(4)]],
    texture2d<half, access::sample> mrMap [[texture(5)]],
    texture2d<half, access::sample> emissiveMap [[texture(6)]],
    texture2d<half, access::sample> occlusionMap [[texture(7)]],
    sampler smp [[sampler(0)]]) {
  // Match Vulkan Texture.cpp material sampling: linear min/mag, no mip usage, anisotropy off.
  constexpr sampler smpMat(address::repeat, filter::linear, mip_filter::none);
  float4 baseColor = float4(baseColorMap.sample(smpMat, v.texCoord).rgba) * mat.baseColorFactor;
  float3 albedo = baseColor.rgb;
  float alpha = baseColor.a;
  float4 mr = float4(mrMap.sample(smpMat, v.texCoord).rgba);
  float metallic = mr.b * mat.metallicFactor;
  float roughness = clamp(mr.g * mat.roughnessFactor, 0.04f, 1.0f);
  float ao = 1.0f + mat.occlusionStrength * (float(occlusionMap.sample(smpMat, v.texCoord).r) - 1.0f);
  float3 emissive = float3(emissiveMap.sample(smpMat, v.texCoord).rgb) * mat.emissiveFactor;
  float3x3 tbn = constructTBN(v.normal, v.tangent);
  float3 N = getNormalFromMap(tbn, normalMap, smpMat, v.texCoord, mat.normalScale);
  float3 V = normalize(v.cameraPos - v.worldPos);
  float3 F0 = mix(float3(0.04f), albedo, metallic);
  float3 Lo = float3(0);
  for (uint i = 0; i < v.lightCount && i < 64u; ++i) {
    LC lc = computeLight(lights[i], v.worldPos);
    float3 L = lc.L;
    float3 radiance = lc.radiance;
    float3 H = normalize(V + L);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);
    float3 spec = D * G * F / (4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 1e-4f);
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float NdotL = max(dot(N, L), 0.0f);
    Lo += (kD * albedo / PI + spec) * radiance * NdotL;
  }
  float3 F = fresnelSchlick(max(dot(N, V), 0.0f), F0);
  float3 kS = F;
  float3 kD = (1.0f - kS) * (1.0f - metallic);
  float3 rotN = float3(N.x, -N.z, N.y);
  float3 irradi = sampleCubemapArray(irradiance, smp, rotN, 0.0f);
  float3 diffuseIBL = irradi * albedo;
  float3 R = reflect(-V, N);
  float3 rotR = float3(R.x, -R.z, R.y);
  const float MAX_REFLECTION_LOD = 4.0f;
  float lod = roughness * MAX_REFLECTION_LOD;
  float3 prefilteredColor = sampleCubemapArray(prefilter, smp, rotR, lod);
  float2 brdf = float2(brdfLUT.sample(smp, float2(max(dot(N, V), 0.0f), roughness)).rg);
  float3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);
  float3 color = (kD * diffuseIBL + specularIBL) * ao + Lo + emissive;
  color = color / (color + 1.0f);
  // Opaque materials must write alpha=1: base-color textures often have a=0; CAMetalLayer then
  // composites the drawable as translucent (flickery see-through). Mask uses discard; blend keeps a.
  float outAlpha = 1.0f;
  if (mat.alphaMode == 1u) {
    if (alpha < mat.alphaCutoff) {
      discard_fragment();
    }
  } else if (mat.alphaMode == 2u) {
    outAlpha = saturate(alpha);
  }
  return float4(color, outAlpha);
}
)msl";

        id<MTLTexture> make1x1Rgba8(id<MTLDevice> dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                    MTLPixelFormat fmt) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                         width:1
                                                                                        height:1
                                                                                     mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            id<MTLTexture> t = [dev newTextureWithDescriptor:d];
            uint8_t px[4] = {r, g, b, a};
            [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:px bytesPerRow:4];
            return t;
        }

        id<MTLTexture> make1x1ArrayFloat(id<MTLDevice> dev, float rv, float gv, float bv) {
            MTLTextureDescriptor* ds =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                   width:1
                                                                  height:1
                                                               mipmapped:NO];
            ds.textureType = MTLTextureType2DArray;
            ds.arrayLength = 6;
            ds.usage = MTLTextureUsageShaderRead;
            ds.storageMode = MTLStorageModeShared;
            id<MTLTexture> tf = [dev newTextureWithDescriptor:ds];
            float px[4] = {rv, gv, bv, 1.f};
            for (NSUInteger f = 0; f < 6; ++f) {
                MTLRegion reg = MTLRegionMake2D(0, 0, 1, 1);
                [tf replaceRegion:reg
                      mipmapLevel:0
                            slice:f
                        withBytes:px
                      bytesPerRow:16
                    bytesPerImage:16];
            }
            return tf;
        }

        id<MTLTexture> makeBrdfNeutral(id<MTLDevice> dev) {
            MTLTextureDescriptor* d =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                   width:1
                                                                  height:1
                                                               mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            id<MTLTexture> t = [dev newTextureWithDescriptor:d];
            float px[4] = {1.f, 1.f, 0.f, 1.f};
            [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:px bytesPerRow:16];
            return t;
        }

        void expandTextureBytesToRgba8(const std::uint8_t* src, int w, int h, int ch,
                                       std::vector<std::uint8_t>& rgbaOut) {
            rgbaOut.resize(static_cast<std::size_t>(w) * h * 4);
            if (ch == 4) {
                std::memcpy(rgbaOut.data(), src, rgbaOut.size());
                return;
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const std::size_t si = (static_cast<std::size_t>(y) * w + x) * ch;
                    const std::size_t di = (static_cast<std::size_t>(y) * w + x) * 4;
                    if (ch == 1) {
                        const std::uint8_t g = src[si];
                        rgbaOut[di + 0] = g;
                        rgbaOut[di + 1] = g;
                        rgbaOut[di + 2] = g;
                        rgbaOut[di + 3] = 255;
                    } else if (ch == 2) {
                        rgbaOut[di + 0] = src[si];
                        rgbaOut[di + 1] = src[si];
                        rgbaOut[di + 2] = src[si];
                        rgbaOut[di + 3] = src[si + 1];
                    } else {
                        rgbaOut[di + 0] = src[si];
                        rgbaOut[di + 1] = src[si + 1];
                        rgbaOut[di + 2] = src[si + 2];
                        rgbaOut[di + 3] = 255;
                    }
                }
            }
        }

        bool uploadModelTextureIfNeeded(id<MTLDevice> dev, scrap::modeling::Texture* tex,
                                        std::unordered_map<std::uintptr_t, id<MTLTexture>>& cache,
                                        id<MTLTexture>& out) {
            if (!tex) {
                return false;
            }
            if (tex->isHDR()) {
                return false;
            }
            std::uintptr_t k = reinterpret_cast<std::uintptr_t>(tex);
            auto it = cache.find(k);
            if (it != cache.end()) {
                out = it->second;
                return true;
            }
            const auto& bytes = tex->getData();
            const int w = tex->getWidth();
            const int h = tex->getHeight();
            const int ch = tex->getChannels();
            if (bytes.empty() || w <= 0 || h <= 0 || ch < 1 || ch > 4) {
                return false;
            }
            const std::size_t expected = static_cast<std::size_t>(w) * h * ch;
            if (bytes.size() < expected) {
                return false;
            }
            std::vector<std::uint8_t> rgbaScratch;
            const std::uint8_t* rgbaPtr = bytes.data();
            if (ch != 4) {
                expandTextureBytesToRgba8(bytes.data(), w, h, ch, rgbaScratch);
                rgbaPtr = rgbaScratch.data();
            }
            MTLPixelFormat fmt =
                tex->isSRGB() ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
            MTLTextureDescriptor* d =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                   width:static_cast<NSUInteger>(w)
                                                                  height:static_cast<NSUInteger>(h)
                                                               mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            id<MTLTexture> mt = [dev newTextureWithDescriptor:d];
            const NSUInteger bpr = static_cast<NSUInteger>(w) * 4u;
            [mt replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(w),
                                              static_cast<NSUInteger>(h))
                  mipmapLevel:0
                    withBytes:rgbaPtr
                  bytesPerRow:bpr];
            cache[k] = mt;
            out = mt;
            return true;
        }

        struct alignas(16) MetalFrameUBO {
            glm::mat4 view{};
            glm::mat4 proj{};
            glm::vec4 cameraPos{0.f, 0.f, 0.f, 0.f};
        };
        static_assert(sizeof(MetalFrameUBO) == 144);
        static_assert(offsetof(MetalFrameUBO, cameraPos) == 128);
        struct alignas(16) MetalDrawPush {
            glm::mat4 model{1.f};
            std::uint32_t lightCount = 0;
            std::uint32_t _p0 = 0;
            std::uint32_t _p1 = 0;
            std::uint32_t _p2 = 0;
        };
        // Must match MSL `MaterialData`: float3 in constant buffers occupies 16 bytes (Metal / std140).
        struct alignas(16) MetalMaterialGPU {
            glm::vec4 baseColorFactor{1.f};
            float metallicFactor = 1.f;
            float roughnessFactor = 1.f;
            float normalScale = 1.f;
            float occlusionStrength = 1.f;
            glm::vec3 emissiveFactor{0.f};
            float _padEmissive{};
            float alphaCutoff = 0.5f;
            std::uint32_t alphaMode = 0; // MaterialProperties::AlphaMode
            std::uint32_t _padA[3] = {};
        };
        static_assert(sizeof(MetalMaterialGPU) % 16 == 0);
        static_assert(offsetof(MetalMaterialGPU, emissiveFactor) == 32);
        static_assert(offsetof(MetalMaterialGPU, alphaCutoff) == 48);
        static_assert(offsetof(MetalMaterialGPU, alphaMode) == 52);
        struct alignas(16) MetalSkyFrame {
            glm::mat4 view{};
            glm::mat4 proj{};
        };

    } // namespace

    struct MetalPBRRenderer::Impl {
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> uploadQueue = nil;
        id<MTLRenderPipelineState> skyPso = nil;
        id<MTLDepthStencilState> skyDepth = nil;
        id<MTLRenderPipelineState> pbrPso = nil;
        id<MTLDepthStencilState> pbrDepth = nil;
        id<MTLSamplerState> sampler = nil;
        MTLVertexDescriptor* vertexDesc = nil;

        id<MTLBuffer> skyVb = nil;
        id<MTLBuffer> skyIb = nil;
        NSUInteger skyIndexCount = 0;

        id<MTLTexture> defBase = nil;
        id<MTLTexture> defNormal = nil;
        id<MTLTexture> defMr = nil;
        id<MTLTexture> defEmissive = nil;
        id<MTLTexture> defOcclusion = nil;

        id<MTLTexture> fallIrr = nil;
        id<MTLTexture> fallPref = nil;
        id<MTLTexture> fallEnv = nil;
        id<MTLTexture> fallBrdf = nil;
        id<MTLBuffer> emptyLightBuf = nil;

        const MetalIBLMaps* ibl = nullptr;
        std::unordered_map<std::uintptr_t, id<MTLTexture>> texCache;
    };

    MetalPBRRenderer::MetalPBRRenderer() : pImpl(std::make_unique<Impl>()) {
    }
    MetalPBRRenderer::~MetalPBRRenderer() {
        shutdown();
    }

    bool MetalPBRRenderer::ready() const {
        return pImpl && pImpl->skyPso != nil && pImpl->pbrPso != nil;
    }

    void MetalPBRRenderer::shutdown() {
        if (!pImpl) {
            return;
        }
        pImpl->texCache.clear();
        pImpl->skyPso = nil;
        pImpl->skyDepth = nil;
        pImpl->pbrPso = nil;
        pImpl->pbrDepth = nil;
        pImpl->sampler = nil;
        pImpl->vertexDesc = nil;
        pImpl->skyVb = nil;
        pImpl->skyIb = nil;
        pImpl->defBase = nil;
        pImpl->defNormal = nil;
        pImpl->defMr = nil;
        pImpl->defEmissive = nil;
        pImpl->defOcclusion = nil;
        pImpl->fallIrr = nil;
        pImpl->fallPref = nil;
        pImpl->fallEnv = nil;
        pImpl->fallBrdf = nil;
        pImpl->emptyLightBuf = nil;
        pImpl->uploadQueue = nil;
        pImpl->device = nil;
        pImpl->ibl = nullptr;
    }

    void MetalPBRRenderer::init(void* mtlDevicePtr, std::uint32_t colorPixelFormatRaw) {
        shutdown();
        id<MTLDevice> dev = (__bridge id<MTLDevice>)mtlDevicePtr;
        if (dev == nil) {
            return;
        }
        pImpl->device = dev;
        pImpl->uploadQueue = [dev newCommandQueue];

        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kPbrMsl]
                                               options:nil
                                                 error:&err];
        if (lib == nil) {
            SCRAP_LOG("Metal", "MetalPBRRenderer shader compile failed: {}",
                      err ? err.localizedDescription.UTF8String : "unknown");
            pImpl->uploadQueue = nil;
            pImpl->device = nil;
            return;
        }

        id<MTLFunction> skyVs = [lib newFunctionWithName:@"sky_vs"];
        id<MTLFunction> skyFs = [lib newFunctionWithName:@"sky_fs"];
        id<MTLFunction> pbrVs = [lib newFunctionWithName:@"pbr_vs"];
        id<MTLFunction> pbrFs = [lib newFunctionWithName:@"pbr_fs"];
        if (!skyVs || !skyFs || !pbrVs || !pbrFs) {
            SCRAP_LOG("Metal",
                      "MetalPBRRenderer missing shader entry points after library compile");
            pImpl->uploadQueue = nil;
            pImpl->device = nil;
            return;
        }

        MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
        vd.attributes[0].format = MTLVertexFormatFloat3;
        vd.attributes[0].offset = 0;
        vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format = MTLVertexFormatFloat3;
        vd.attributes[1].offset = 12;
        vd.attributes[1].bufferIndex = 0;
        vd.attributes[2].format = MTLVertexFormatFloat2;
        vd.attributes[2].offset = 24;
        vd.attributes[2].bufferIndex = 0;
        vd.attributes[3].format = MTLVertexFormatFloat3;
        vd.attributes[3].offset = 32;
        vd.attributes[3].bufferIndex = 0;
        vd.attributes[4].format = MTLVertexFormatFloat4;
        vd.attributes[4].offset = 44;
        vd.attributes[4].bufferIndex = 0;
        vd.layouts[0].stride = 60;
        vd.layouts[0].stepRate = 1;
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        pImpl->vertexDesc = vd;

        MTLVertexDescriptor* skyVd = [[MTLVertexDescriptor alloc] init];
        skyVd.attributes[0].format = MTLVertexFormatFloat3;
        skyVd.attributes[0].offset = 0;
        skyVd.attributes[0].bufferIndex = 0;
        skyVd.layouts[0].stride = 12;
        skyVd.layouts[0].stepRate = 1;
        skyVd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* spd = [[MTLRenderPipelineDescriptor alloc] init];
        spd.label = @"scrap_skybox";
        spd.vertexFunction = skyVs;
        spd.fragmentFunction = skyFs;
        spd.vertexDescriptor = skyVd;
        spd.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(colorPixelFormatRaw);
        spd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        spd.colorAttachments[0].blendingEnabled = NO;
        pImpl->skyPso = [dev newRenderPipelineStateWithDescriptor:spd error:&err];
        if (pImpl->skyPso == nil) {
            SCRAP_LOG("Metal", "MetalPBRRenderer sky pipeline creation failed: {}",
                      err ? err.localizedDescription.UTF8String : "unknown");
            pImpl->uploadQueue = nil;
            pImpl->device = nil;
            return;
        }

        MTLDepthStencilDescriptor* sds = [[MTLDepthStencilDescriptor alloc] init];
        sds.depthCompareFunction = MTLCompareFunctionLessEqual;
        sds.depthWriteEnabled = NO;
        pImpl->skyDepth = [dev newDepthStencilStateWithDescriptor:sds];

        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.label = @"scrap_pbr";
        pd.vertexFunction = pbrVs;
        pd.fragmentFunction = pbrFs;
        pd.vertexDescriptor = vd;
        pd.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(colorPixelFormatRaw);
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        // Match Vulkan main PBR pipeline (GraphicsPipelineConfig::enableBlending = true).
        pd.colorAttachments[0].blendingEnabled = YES;
        pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
        pd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pImpl->pbrPso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (pImpl->pbrPso == nil) {
            SCRAP_LOG("Metal", "MetalPBRRenderer PBR pipeline creation failed: {}",
                      err ? err.localizedDescription.UTF8String : "unknown");
            pImpl->uploadQueue = nil;
            pImpl->device = nil;
            return;
        }

        MTLDepthStencilDescriptor* dds = [[MTLDepthStencilDescriptor alloc] init];
        dds.depthCompareFunction = MTLCompareFunctionLess;
        dds.depthWriteEnabled = YES;
        pImpl->pbrDepth = [dev newDepthStencilStateWithDescriptor:dds];

        MTLSamplerDescriptor* samp = [[MTLSamplerDescriptor alloc] init];
        samp.minFilter = MTLSamplerMinMagFilterLinear;
        samp.magFilter = MTLSamplerMinMagFilterLinear;
        samp.mipFilter = MTLSamplerMipFilterLinear;
        samp.sAddressMode = MTLSamplerAddressModeRepeat;
        samp.tAddressMode = MTLSamplerAddressModeRepeat;
        samp.rAddressMode = MTLSamplerAddressModeClampToEdge;
        samp.maxAnisotropy = 1;
        pImpl->sampler = [dev newSamplerStateWithDescriptor:samp];

        static const float cubeVerts[] = {
            -1.f, -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f,
            -1.f, -1.f, 1.f,  1.f, -1.f, 1.f,  1.f, 1.f, 1.f,  -1.f, 1.f, 1.f,
        };
        static const uint16_t cubeIdx[] = {
            0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 0, 4, 7, 7, 3, 0,
            1, 5, 6, 6, 2, 1, 3, 2, 6, 6, 7, 3, 0, 1, 5, 5, 4, 0,
        };
        pImpl->skyVb = [dev newBufferWithBytes:cubeVerts
                                        length:sizeof(cubeVerts)
                                       options:MTLResourceStorageModeShared];
        pImpl->skyIb = [dev newBufferWithBytes:cubeIdx
                                        length:sizeof(cubeIdx)
                                       options:MTLResourceStorageModeShared];
        pImpl->skyIndexCount = sizeof(cubeIdx) / sizeof(cubeIdx[0]);

        pImpl->defBase = make1x1Rgba8(dev, 255, 255, 255, 255, MTLPixelFormatRGBA8Unorm_sRGB);
        pImpl->defNormal = make1x1Rgba8(dev, 128, 128, 255, 255, MTLPixelFormatRGBA8Unorm);
        pImpl->defMr = make1x1Rgba8(dev, 255, 255, 0, 255, MTLPixelFormatRGBA8Unorm);
        pImpl->defEmissive = make1x1Rgba8(dev, 0, 0, 0, 255, MTLPixelFormatRGBA8Unorm);
        pImpl->defOcclusion = make1x1Rgba8(dev, 255, 255, 255, 255, MTLPixelFormatRGBA8Unorm);

        pImpl->fallEnv = make1x1ArrayFloat(dev, 0.03f, 0.04f, 0.06f);
        pImpl->fallIrr = make1x1ArrayFloat(dev, 0.03f, 0.04f, 0.06f);
        pImpl->fallPref = make1x1ArrayFloat(dev, 0.03f, 0.04f, 0.06f);
        pImpl->fallBrdf = makeBrdfNeutral(dev);
        scrap::GPULight z{};
        pImpl->emptyLightBuf = [dev newBufferWithBytes:&z
                                                length:sizeof(z)
                                               options:MTLResourceStorageModeShared];
    }

    void MetalPBRRenderer::setIBL(const MetalIBLMaps* maps) {
        if (pImpl) {
            pImpl->ibl = maps;
        }
    }

    void MetalPBRRenderer::draw(void* encPtr, float viewportWidth, float viewportHeight,
                                const glm::mat4& view, const glm::mat4& proj,
                                const glm::vec3& cameraPos, const scrap::GPULight* lights,
                                std::uint32_t lightCount, const MetalDrawablePBR* drawables,
                                std::size_t drawableCount) {
        if (!pImpl || !pImpl->skyPso || !pImpl->pbrPso || encPtr == nullptr) {
            return;
        }
        id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)encPtr;
        if (viewportWidth <= 0.f || viewportHeight <= 0.f) {
            return;
        }

        id<MTLDevice> dev = pImpl->device;
        id<MTLTexture> envT = pImpl->fallEnv;
        id<MTLTexture> irrT = pImpl->fallIrr;
        id<MTLTexture> prefT = pImpl->fallPref;
        id<MTLTexture> brdfT = pImpl->fallBrdf;
        id<MTLSamplerState> iblSmp = nil;
        if (pImpl->ibl && pImpl->ibl->envMapArray) {
            envT = (__bridge id<MTLTexture>)pImpl->ibl->envMapArray;
        }
        if (pImpl->ibl && pImpl->ibl->irradianceArray) {
            irrT = (__bridge id<MTLTexture>)pImpl->ibl->irradianceArray;
        }
        if (pImpl->ibl && pImpl->ibl->prefilterArray) {
            prefT = (__bridge id<MTLTexture>)pImpl->ibl->prefilterArray;
        }
        if (pImpl->ibl && pImpl->ibl->brdfLUT) {
            brdfT = (__bridge id<MTLTexture>)pImpl->ibl->brdfLUT;
        }
        if (pImpl->ibl && pImpl->ibl->samplerState) {
            iblSmp = (__bridge id<MTLSamplerState>)pImpl->ibl->samplerState;
        } else {
            iblSmp = pImpl->sampler;
        }

        MTLViewport vp{};
        vp.originX = 0;
        vp.originY = 0;
        vp.width = viewportWidth;
        vp.height = viewportHeight;
        vp.znear = 0.0;
        vp.zfar = 1.0;
        [enc setViewport:vp];

        // GLM `perspective` targets OpenGL NDC (z in [-1,1] after divide). Metal expects z in [0,1]; without this,
        // depth tests break (missing surfaces, wrong lighting order). Y matches GLM (no Vulkan Y-flip).
        glm::mat4 zClipToMetal(1.f);
        zClipToMetal[2][2] = 0.5f;
        zClipToMetal[3][2] = 0.5f;
        const glm::mat4 projMetal = zClipToMetal * proj;

        MetalSkyFrame skyFr{};
        skyFr.view = view;
        skyFr.proj = projMetal;
        [enc setRenderPipelineState:pImpl->skyPso];
        [enc setDepthStencilState:pImpl->skyDepth];
        [enc setVertexBuffer:pImpl->skyVb offset:0 atIndex:0];
        [enc setVertexBytes:&skyFr length:sizeof(skyFr) atIndex:1];
        [enc setFragmentTexture:envT atIndex:0];
        [enc setFragmentSamplerState:iblSmp atIndex:0];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:pImpl->skyIndexCount
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:pImpl->skyIb
                 indexBufferOffset:0];

        MetalFrameUBO frame{};
        frame.view = view;
        frame.proj = projMetal;
        frame.cameraPos = glm::vec4(cameraPos, 0.0f);

        const std::uint32_t lc =
            static_cast<std::uint32_t>(std::min<std::size_t>(lightCount, kMaxLights));
        const std::size_t lightBytes = static_cast<std::size_t>(lc) * sizeof(scrap::GPULight);
        id<MTLBuffer> lightBuf = pImpl->emptyLightBuf;
        if (lc > 0 && lights != nullptr) {
            lightBuf = [dev newBufferWithBytes:lights
                                        length:lightBytes
                                       options:MTLResourceStorageModeShared];
        }

        [enc setRenderPipelineState:pImpl->pbrPso];
        [enc setDepthStencilState:pImpl->pbrDepth];
        [enc setFragmentSamplerState:pImpl->sampler atIndex:0];
        // glTF front faces are CCW. Using CW here makes meshes render inside-out under back-face culling.
        [enc setFrontFacingWinding:MTLWindingCounterClockwise];

        for (std::size_t di = 0; di < drawableCount; ++di) {
            const MetalDrawablePBR& d = drawables[di];
            if (d.mesh.indexCount == 0 || d.mesh.vertexBuffer == nullptr ||
                d.mesh.indexBuffer == nullptr) {
                continue;
            }
            id<MTLBuffer> vb = (__bridge id<MTLBuffer>)d.mesh.vertexBuffer;
            id<MTLBuffer> ib = (__bridge id<MTLBuffer>)d.mesh.indexBuffer;

            [enc setRenderPipelineState:pImpl->pbrPso];
            [enc setCullMode:d.doubleSided ? MTLCullModeNone : MTLCullModeBack];

            MetalDrawPush push{};
            push.model = d.model;
            push.lightCount = lc;

            [enc setVertexBuffer:vb offset:0 atIndex:0];
            [enc setVertexBytes:&frame length:sizeof(frame) atIndex:1];
            [enc setVertexBytes:&push length:sizeof(push) atIndex:2];

            [enc setFragmentTexture:irrT atIndex:0];
            [enc setFragmentTexture:prefT atIndex:1];
            [enc setFragmentTexture:brdfT atIndex:2];
            [enc setFragmentBuffer:lightBuf offset:0 atIndex:3];

            id<MTLTexture> tBase = pImpl->defBase;
            id<MTLTexture> tNor = pImpl->defNormal;
            id<MTLTexture> tMr = pImpl->defMr;
            id<MTLTexture> tEmi = pImpl->defEmissive;
            id<MTLTexture> tOcc = pImpl->defOcclusion;
            MetalMaterialGPU matGpu{};
            if (d.material) {
                const auto& p = d.material->getProperties();
                matGpu.baseColorFactor = p.baseColorFactor;
                matGpu.metallicFactor = p.metallicFactor;
                matGpu.roughnessFactor = p.roughnessFactor;
                matGpu.normalScale = p.normalScale;
                matGpu.occlusionStrength = p.occlusionStrength;
                matGpu.emissiveFactor = p.emissiveFactor;
                matGpu.alphaCutoff = p.alphaCutoff;
                matGpu.alphaMode = static_cast<std::uint32_t>(p.alphaMode);
                id<MTLTexture> tmp = nil;
                if (auto pt = d.material->getTexture(scrap::modeling::TextureType::BaseColor)) {
                        uploadModelTextureIfNeeded(dev, pt.get(), pImpl->texCache,
                                                   tmp);
                    if (tmp)
                        tBase = tmp;
                }
                if (auto pt = d.material->getTexture(scrap::modeling::TextureType::Normal)) {
                        uploadModelTextureIfNeeded(dev, pt.get(), pImpl->texCache,
                                                   tmp);
                    if (tmp)
                        tNor = tmp;
                }
                if (auto pt =
                        d.material->getTexture(scrap::modeling::TextureType::MetallicRoughness)) {
                        uploadModelTextureIfNeeded(dev, pt.get(), pImpl->texCache,
                                                   tmp);
                    if (tmp)
                        tMr = tmp;
                }
                if (auto pt = d.material->getTexture(scrap::modeling::TextureType::Emissive)) {
                        uploadModelTextureIfNeeded(dev, pt.get(), pImpl->texCache,
                                                   tmp);
                    if (tmp)
                        tEmi = tmp;
                }
                if (auto pt = d.material->getTexture(scrap::modeling::TextureType::Occlusion)) {
                        uploadModelTextureIfNeeded(dev, pt.get(), pImpl->texCache,
                                                   tmp);
                    if (tmp)
                        tOcc = tmp;
                }
            }

            [enc setFragmentTexture:tBase atIndex:3];
            [enc setFragmentTexture:tNor atIndex:4];
            [enc setFragmentTexture:tMr atIndex:5];
            [enc setFragmentTexture:tEmi atIndex:6];
            [enc setFragmentTexture:tOcc atIndex:7];
            [enc setFragmentBytes:&matGpu length:sizeof(matGpu) atIndex:4];

            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:d.mesh.indexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:ib
                     indexBufferOffset:0];
        }
    }

} // namespace scrap::gpu::metal
