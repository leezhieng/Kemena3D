/**
 * @file ktexture2d.h
 * @brief 2D texture with pixel data access.
 */

#ifndef KTEXTURE2D_H
#define KTEXTURE2D_H

#include "kexport.h"

#include "ktexture.h"

namespace kemena
{
    /**
     * @brief Extends kTexture with 2D-specific metadata and optional CPU-side pixel data.
     *
     * Pixel data is optionally retained after upload (when keepData is true in
     * kAssetManager::loadTexture2D) for later CPU access.
     */
    class KEMENA3D_API kTexture2D : public kTexture
    {
    public:
        /**
         * @brief Constructs an empty 2D texture with zeroed metadata and no pixel data.
         */
        kTexture2D();

        /**
         * @brief Destroys the 2D texture and releases any retained CPU-side pixel data.
         */
        virtual ~kTexture2D();

        /**
         * @brief Sets the texture width.
         * @param newWidth Width in pixels.
         */
        void setWidth(int newWidth);

        /**
         * @brief Returns the texture width in pixels.
         * @return Width.
         */
        int getWidth();

        /**
         * @brief Sets the texture height.
         * @param newHeight Height in pixels.
         */
        void setHeight(int newHeight);

        /**
         * @brief Returns the texture height in pixels.
         * @return Height.
         */
        int getHeight();

        /**
         * @brief Sets the number of colour channels.
         * @param newChannel Channel count (1=grey, 3=RGB, 4=RGBA).
         */
        void setChannels(int newChannel);

        /**
         * @brief Returns the number of colour channels.
         * @return Channel count.
         */
        int getChannels();

        /**
         * @brief Stores a pointer to the CPU-side pixel data.
         * @param newData Pointer to raw pixel bytes; caller manages lifetime.
         */
        void setData(unsigned char *newData);

        /**
         * @brief Returns the CPU-side pixel data pointer.
         * @return Raw pixel bytes, or nullptr if not retained.
         */
        unsigned char *getData();

    protected:
    private:
        int width;    ///< Texture width in pixels.
        int height;   ///< Texture height in pixels.
        int channels; ///< Number of colour channels.

        unsigned char *data; ///< Optional CPU-side pixel buffer.
    };
}

#endif // KTEXTURE2D_H
