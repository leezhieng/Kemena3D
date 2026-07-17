/**
 * @file ktexture.h
 * @brief Base class for GPU texture objects.
 */

#ifndef KTEXTURE_H
#define KTEXTURE_H

#include "kexport.h"

#include <iostream>
#include <string>
#include <vector>

#include "glm/glm.hpp"
#include "kdatatype.h"

namespace kemena
{
    /**
     * @brief Base GPU texture abstraction shared by kTexture2D and kTextureCube.
     *
     * Stores the API-agnostic texture handle (uint32_t), a name used to bind
     * the texture to shader uniforms, and the texture type (2D or cube map).
     * No GL headers are included — all GPU operations go through kDriver.
     */
    class KEMENA3D_API kTexture
    {
    public:
        /**
         * @brief Constructs an empty texture with no GPU handle assigned.
         */
        kTexture();

        /**
         * @brief Virtual destructor; allows safe polymorphic deletion of derived textures.
         */
        virtual ~kTexture();

        /**
         * @brief Creates a shallow copy with a different sampler name.
         * @param newName New texture name for the duplicate.
         * @return Heap-allocated duplicate; caller takes ownership.
         */
        kTexture *duplicate(kString newName);

        /**
         * @brief Sets the GPU texture handle.
         * @param newTextureID Handle returned by the texture loading API.
         */
        void setTextureID(uint32_t newTextureID);

        /**
         * @brief Returns the GPU texture handle.
         * @return Opaque texture handle (0 if not yet loaded).
         */
        uint32_t getTextureID();

        /**
         * @brief Sets the GLSL sampler uniform name for this texture.
         * @param newName Uniform variable name in the shader.
         */
        void setTextureName(kString newName);

        /**
         * @brief Returns the GLSL sampler uniform name.
         * @return Uniform name kString.
         */
        kString getTextureName();

        /**
         * @brief Sets the texture type (2D or cube map).
         * @param newType Texture type.
         */
        void setType(kTextureType newType);

        /**
         * @brief Returns the texture type.
         * @return TEX_TYPE_2D or TEX_TYPE_CUBE.
         */
        kTextureType getType();

    protected:
    private:
        uint32_t textureID = 0;     ///< GPU texture handle (backend-agnostic).
        kString textureName;         ///< GLSL sampler uniform name.
        kTextureType type = kTextureType::TEX_TYPE_2D; ///< Texture dimensionality.
    };
}

#endif // KTEXTURE_H
