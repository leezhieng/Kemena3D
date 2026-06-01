/**
 * @file ktexturecube.h
 * @brief Cube-map texture used for skyboxes and environment maps.
 */

#ifndef KTEXTURECUBE_H
#define KTEXTURECUBE_H

#include "kexport.h"

#include "ktexture.h"

namespace kemena
{
    /**
     * @brief Represents a six-face cube-map texture.
     *
     * Loaded via kAssetManager::loadTextureCube(). Bind it using
     * kDriver::bindTextureCube() and sample in GLSL with a
     * @c samplerCube uniform.
     */
    class KEMENA3D_API kTextureCube : public kTexture
    {
    public:
        /** @brief Constructs an empty cube-map texture. */
        kTextureCube();

        /** @brief Destroys the cube-map texture and releases its GPU resources. */
        virtual ~kTextureCube();

    protected:
    private:
    };
}

#endif // KTEXTURECUBE_H
