#include "app/modeling/Texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>

namespace scrap {
    namespace modeling {

        Texture::Texture(const std::string& path, TextureType type, bool sRGB)
            : path(path), type(type), sRGB(sRGB), hdr(false), embedded(false), width(0), height(0),
              channels(0), dataLoaded(false) {
        }

        Texture::Texture(const std::vector<unsigned char>& data, int width, int height,
                         int channels, TextureType type, bool sRGB, const std::string& name)
            : path(name), type(type), sRGB(sRGB), hdr(false), embedded(true), width(width),
              height(height), channels(channels), data(data), dataLoaded(true) {
        }

        const std::vector<unsigned char>& Texture::getData() {
            if (!dataLoaded) {
                loadFromFile();
            }
            return data;
        }

        const std::vector<float>& Texture::getHDRData() {
            if (!dataLoaded) {
                loadFromFile();
            }
            return hdrData;
        }

        void Texture::loadFromFile() {
            if (dataLoaded) {
                return;
            }

            if (embedded) {
                return;
            }

            if (stbi_is_hdr(path.c_str())) {
                loadFromFileHDR();
                return;
            }

            int w, h, c;
            unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);

            if (!pixels) {
                createDefaultPixels();
                return;
            }

            width = w;
            height = h;
            channels = 4;

            size_t dataSize = width * height * channels;
            data.resize(dataSize);
            std::memcpy(data.data(), pixels, dataSize);

            stbi_image_free(pixels);
            dataLoaded = true;
        }

        void Texture::loadFromFileHDR() {
            int w, h, c;
            float* pixels = stbi_loadf(path.c_str(), &w, &h, &c, STBI_rgb_alpha);

            if (!pixels) {
                createDefaultPixels();
                return;
            }

            width = w;
            height = h;
            channels = 4;
            hdr = true;

            size_t pixelCount = static_cast<size_t>(width) * height * channels;
            hdrData.resize(pixelCount);
            std::memcpy(hdrData.data(), pixels, pixelCount * sizeof(float));

            stbi_image_free(pixels);
            dataLoaded = true;
        }

        void Texture::createDefaultPixels() {
            width = 1;
            height = 1;
            channels = 4;
            data.resize(4);

            if (type == TextureType::Normal) {
                data[0] = 128;
                data[1] = 128;
                data[2] = 255;
                data[3] = 255;
            } else if (type == TextureType::MetallicRoughness || type == TextureType::Occlusion) {
                data[0] = 0;
                data[1] = 0;
                data[2] = 0;
                data[3] = 255;
            } else {
                data[0] = 255;
                data[1] = 255;
                data[2] = 255;
                data[3] = 255;
            }

            dataLoaded = true;
        }

    } // namespace modeling
} // namespace scrap
