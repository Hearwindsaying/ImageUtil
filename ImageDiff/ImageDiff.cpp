
#include "freeImage/FreeImagePlus.h"

#include <iostream>
#include <locale>
#include <string>
#include <cassert>
#include <vector>
#include <algorithm>

using namespace std;

namespace ImageUtil
{
    double luminance(float r, float g, float b)
    {
        return 0.212671f * r + 0.715160f * g + 0.072169f * b;
    }

    template<typename T>
    std::vector<T> diffVector(const std::vector<T>& v1, const std::vector<T>& v2)
    {
        assert(v1.size() == v2.size());
        
        std::vector<T> res;
        res.reserve(v1.size());
        for (int i = 0; i < v1.size(); ++i)
        {
            res.push_back(std::abs(v1[i] - v2[i]));
        }
        return res;
    }

    /**
     * @brief Utility class for loading image and compute RMSE.
     */
    class ImageRMSE
    {
    public:
        /**
         * @brief Compute RMSE and output directly.
         */
        static void computeRMSE(const std::string & data1, const std::string & data2, const std::string & ref, bool diffImage = false)
        {
            int width, height;
            auto image1 = loadImageToLuminance(data1, &width, &height);
            auto image2 = loadImageToLuminance(data2, &width, &height);
            auto imageRef = loadImageToLuminance(ref, &width, &height);
            assert(!image1.empty() || !image2.empty() || !imageRef.empty());

            double rmse1 = rmse(image1, imageRef);
            double rmse2 = rmse(image2, imageRef);

            std::cout.precision(std::numeric_limits<double>::max_digits10);
            std::cout << "Image1 RMSE: " << rmse1 << std::endl << "Image2 RMSE: " << rmse2 << std::endl;

            if (diffImage)
            {
                auto max1 = maxDiff(image1, imageRef);
                auto max2 = maxDiff(image2, imageRef);
                std::cout << "Image1 maxDiff at: " << max1.first << " value: " << max1.second << std::endl
                          << "Image2 maxDiff at: " << max2.first << " value: " << max2.second << std::endl;

                /* Diff image. */
                auto diff1 = diffVector(image1, imageRef);
                auto diff2 = diffVector(image2, imageRef);
                saveLuminanceImage(diff1, width, height, "diff1.exr");
                saveLuminanceImage(diff2, width, height, "diff2.exr");
            }
        }

        /**
         * @brief For 32-bpc HDR/OpenEXR file only.
         */
        static double computeRMSE(const std::string & filename1, const std::string & filename2)
        {
            int width, height;
            auto image1 = loadImageToLuminance(filename1, &width, &height);
            auto image2 = loadImageToLuminance(filename2, &width, &height);
            assert(!image1.empty() || !image2.empty());
        
            return rmse(image1, image2);
        }

    private:
        static std::pair<int,double> maxDiff(const std::vector<double> &data1, const std::vector<double> &data2)
        {
            /* index, value */
            std::pair<int, double> res = std::make_pair(0, std::abs(data1[0] - data2[0]));
            
            for (int i = 0; i < data1.size(); ++i)
            {
                if (std::abs(data1[i] - data2[i]) > res.second)
                {
                    res.second = std::abs(data1[i] - data2[i]);
                    res.first = i;
                }
                    
            }
            return res;
        }

        static double rmse(const std::vector<double> &data1, const std::vector<double> &data2)
        {
            double rmse = 0.0;
            for (int i = 0; i < data1.size(); ++i)
            {
                rmse += (data1[i] - data2[i])*(data1[i] - data2[i]);
            }
            rmse = sqrt(1.0 / data1.size() * rmse);

            return rmse;
        }

        /**
         * @brief Load 32bpc HDR/OpenEXR image and convert RGB channels to luminance.
         * @param[out] width 
         * @param[out] height
         * @return Empty vector if fails.
         */
        static std::vector<double> loadImageToLuminance(const std::string & filename, int *width, int *height)
        {
            /* Load image using FreeImage. */

            // Extension check.
            auto getFreeImageFormat = [&filename]()->FREE_IMAGE_FORMAT
            {
                // Get the filename extension                                                
                std::string::size_type extension_index = filename.find_last_of(".");
                std::string ext = extension_index != std::string::npos ?
                    filename.substr(extension_index + 1) :
                    std::string();
                std::locale loc;
                for (std::string::size_type i = 0; i < ext.length(); ++i)
                    ext[i] = std::tolower(ext[i], loc);
                if (ext == "hdr")
                    return FIF_HDR;
                if (ext == "exr")
                    return FIF_EXR;
                return FIF_UNKNOWN;
            };

            auto imageFormat = getFreeImageFormat();
            if (imageFormat == FIF_UNKNOWN)
            {
                std::cerr << "The format is neither HDR nor EXR, not supported for RMSE computation..." << std::endl;
                return std::vector<double>();
            }

            FIBITMAP* bitmap = FreeImage_Load(imageFormat, filename.c_str());
            assert(bitmap != nullptr);

            // How many bits-per-pixel is the source image?
            int bitsPerPixel = FreeImage_GetBPP(bitmap);
            int imageWidth = FreeImage_GetWidth(bitmap);
            int imageHeight = FreeImage_GetHeight(bitmap);
            *width = imageWidth;
            *height = imageHeight;

            FREE_IMAGE_TYPE imageType = FreeImage_GetImageType(bitmap);
            int bytespp = FreeImage_GetLine(bitmap) / imageWidth / sizeof(float);

            std::cout << "Image: " << filename << " is size: " << imageWidth << "x" << imageHeight << "with " << bitsPerPixel << "bits per pixel" << "." << std::endl;
            std::cout << "Image Type: " << imageType << std::endl;
            std::cout << "Image component(which is used to step to the next pixel):" << bytespp << std::endl;

            // Caveat: BITMAP scanline is upside down 
            //         -- doesn't matter for RMSE computation however.

            std::vector<double> luminanceBuffer;
            luminanceBuffer.reserve(imageWidth*imageHeight);

            switch (imageType)
            {
            case FIT_RGBAF:
                for (auto y = 0; y < imageHeight; ++y)
                {
                    // Note the scanline fetched by FreeImage is upside down--the first scanline corresponds to the buttom of the image!
                    FLOAT *bits = reinterpret_cast<FLOAT *>(FreeImage_GetScanLine(bitmap, imageHeight - y - 1));

                    for (auto x = 0; x < imageWidth; ++x)
                    {
                        unsigned int buf_index = (imageWidth * y + x) * 4;

                        assert(!isinf(static_cast<float>(bits[0])) && !isnan(static_cast<float>(bits[0])));
                        assert(!isinf(static_cast<float>(bits[1])) && !isnan(static_cast<float>(bits[1])));
                        assert(!isinf(static_cast<float>(bits[2])) && !isnan(static_cast<float>(bits[2])));

                        // note that for RGBAF/RGBF format, the pixel order is:RGB(A)
                        luminanceBuffer.push_back(luminance(static_cast<float>(bits[0]), static_cast<float>(bits[1]), static_cast<float>(bits[2])));
                        // jump to next pixel
                        bits += bytespp;
                    }
                }
                break;

            case FIT_RGBF:
                for (auto y = 0; y < imageHeight; ++y)
                {
                    FLOAT *bits = reinterpret_cast<FLOAT *>(FreeImage_GetScanLine(bitmap, imageHeight - y - 1));

                    for (auto x = 0; x < imageWidth; ++x)
                    {
                        unsigned int buf_index = (imageWidth * y + x) * 4;

                        assert(!isinf(static_cast<float>(bits[0])) && !isnan(static_cast<float>(bits[0])));
                        assert(!isinf(static_cast<float>(bits[1])) && !isnan(static_cast<float>(bits[1])));
                        assert(!isinf(static_cast<float>(bits[2])) && !isnan(static_cast<float>(bits[2])));

                        luminanceBuffer.push_back(luminance(static_cast<float>(bits[0]), static_cast<float>(bits[1]), static_cast<float>(bits[2])));
                        // jump to next pixel
                        bits += bytespp;
                    }
                }
                break;
            default:
                std::cerr << "Type of the image is not RGBF/RGBAF, not supported yet..." << std::endl;
                break;
            }

            assert(luminanceBuffer.size() == imageWidth * imageHeight);
            // Unload the 32-bit colour bitmap
            FreeImage_Unload(bitmap);

            return luminanceBuffer;
        }

        /**
         * @brief Save luminance (R=G=B) buffer to OpenEXR image.
         */
        static void saveLuminanceImage(const std::vector<double> &buffer, int width, int height, const std::string & filename)
        {
            FIBITMAP* bitmap = FreeImage_AllocateT(FIT_RGBAF, width, height);

            int bitsPerPixel = FreeImage_GetBPP(bitmap);
            FREE_IMAGE_TYPE imageType = FreeImage_GetImageType(bitmap);
            int bytespp = FreeImage_GetLine(bitmap) / width / sizeof(float);

            for (auto y = 0; y < height; ++y)
            {
                //Note the scanline fetched by FreeImage is upside down--the first scanline corresponds to the buttom of the image!
                FLOAT *bits = reinterpret_cast<FLOAT *>(FreeImage_GetScanLine(bitmap, height - y - 1));

                for (auto x = 0; x < width; ++x)
                {
                    unsigned int buf_index = (width * y + x) * 1;

                    //32bit image texture is linear.
                    bits[0] = buffer[buf_index];
                    bits[1] = buffer[buf_index];
                    bits[2] = buffer[buf_index];
                    bits[3] = 1.f;
                    // jump to next pixel
                    bits += bytespp;
                }
            }

            FreeImage_Save(FIF_EXR, bitmap, filename.c_str());
            // Unload the 32-bit colour bitmap
            FreeImage_Unload(bitmap);
        }
    };
}

using namespace ImageUtil;


int main(int argc, char* argv[])
{
    // Check the number of parameters
    if (argc < 4) {
        // Tell the user how to run the program
        std::cerr << "RMSE Sample Usage: " << argv[0] << " image1.exr image2.exr refImage.exr <true>" << std::endl;
        /* "Usage messages" are a conventional way of telling the user
         * how to run a program if they enter the command incorrectly.
         */
        return 1;
    }

    ImageRMSE::computeRMSE(argv[1], argv[2], argv[3], argc==5);
    
    return 0;
}
