#pragma once

#include <memory>
#include <string>
#include <vector>

namespace scrap {
    namespace modeling {

        enum class TextureType {
            BaseColor,
            Normal,
            MetallicRoughness,
            Occlusion,
            Emissive,
            EnvironmentMapHDR,
            Unknown
        };

        class Texture {
          public:
            Texture(const std::string& path, TextureType type, bool sRGB);

            Texture(const std::vector<unsigned char>& data, int width, int height, int channels,
                    TextureType type, bool sRGB, const std::string& name = "embedded");

            const std::string& getPath() const {
                return path;
            }
            TextureType getType() const {
                return type;
            }
            bool isSRGB() const {
                return sRGB;
            }
            bool isHDR() const {
                return hdr;
            }
            int getWidth() const {
                return width;
            }
            int getHeight() const {
                return height;
            }
            int getChannels() const {
                return channels;
            }
            bool isEmbedded() const {
                return embedded;
            }
            bool hasGPUData() const {
                return false;
            }

            const std::vector<unsigned char>& getData();
            const std::vector<float>& getHDRData();

          private:
            std::string path;
            TextureType type;
            bool sRGB;
            bool hdr;
            bool embedded;

            int width;
            int height;
            int channels;
            std::vector<unsigned char> data;
            std::vector<float> hdrData;
            bool dataLoaded;

            void loadFromFile();
            void loadFromFileHDR();
            void createDefaultPixels();
        };

    } // namespace modeling
} // namespace scrap
