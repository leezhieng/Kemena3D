/**
 * @file kmaterial.h
 * @brief Surface material combining a shader, textures, and PBR/Phong properties.
 */

#ifndef KMATERIAL_H
#define KMATERIAL_H

#include "kexport.h"

#include "kdatatype.h"
#include "kshader.h"
#include "ktexture2d.h"
#include "ktexturecube.h"

namespace kemena
{
    /**
     * @brief Groups a shader program with textures and surface parameters.
     *
     * A material is assigned to a kObject (and optionally its children) via
     * kObject::setMaterial().  The renderer queries the material for the shader
     * to bind, the texture list to upload, and the surface colour/PBR values
     * to pass as uniforms.
     */
    class kMaterial
    {
    public:
        /**
         * @brief Constructs a material with default surface parameters and no shader.
         */
        kMaterial();

        /**
         * @brief Destroys the material.
         *
         * Does not free the shader or textures; ownership of those resources
         * remains with the caller.
         */
        virtual ~kMaterial();

        /**
         * @brief Assigns a compiled shader to this material.
         * @param newShader Pointer to the shader program; must outlive the material.
         */
        void setShader(kShader *newShader);

        /**
         * @brief Returns the assigned shader.
         * @return Pointer to the shader, or nullptr if none is set.
         */
        kShader *getShader();

        /**
         * @brief Appends a texture to the material's texture list.
         * @param texture Texture to add; ownership is not transferred.
         */
        void addTexture(kTexture *texture);

        /**
         * @brief Returns all textures attached to this material.
         * @return Copy of the internal texture vector.
         */
        std::vector<kTexture *> getTextures();

        /**
         * @brief Returns a texture by index.
         * @param index Zero-based index into the texture list.
         * @return Pointer to the texture, or nullptr if index is out of range.
         */
        kTexture *getTexture(int index);

        /**
         * @brief Sets the transparency blending mode.
         * @param type Transparency type (none, alpha blend, etc.).
         */
        void setTransparent(kTransparentType type);

        /**
         * @brief Returns the transparency blending mode.
         * @return Current transparency type.
         */
        kTransparentType getTransparent();

        /**
         * @brief Sets the UV tiling factor applied to all texture coordinates.
         * @param newTiling Tiling multiplier (U, V).
         */
        void setUvTiling(kVec2 newTiling);

        /**
         * @brief Returns the UV tiling factor.
         * @return UV tiling (U, V).
         */
        kVec2 getUvTiling();

        /**
         * @brief Sets the ambient colour.
         * @param color RGB ambient colour (0..1 per channel).
         */
        void setAmbientColor(kVec3 color);

        /**
         * @brief Sets the diffuse colour.
         * @param color RGB diffuse colour.
         */
        void setDiffuseColor(kVec3 color);

        /**
         * @brief Sets the specular colour.
         * @param color RGB specular colour.
         */
        void setSpecularColor(kVec3 color);

        /**
         * @brief Sets the Phong shininess exponent.
         * @param value Shininess exponent (higher = tighter highlights).
         */
        void setShininess(float value);

        /**
         * @brief Sets the PBR metallic factor.
         * @param value Metallic value in the range [0, 1].
         */
        void setMetallic(float value);

        /**
         * @brief Sets the PBR roughness factor.
         * @param value Roughness value in the range [0, 1].
         */
        void setRoughness(float value);

        /**
         * @brief Returns the ambient colour.
         * @return RGB ambient colour.
         */
        kVec3 getAmbientColor();

        /**
         * @brief Returns the diffuse colour.
         * @return RGB diffuse colour.
         */
        kVec3 getDiffuseColor();

        /**
         * @brief Returns the specular colour.
         * @return RGB specular colour.
         */
        kVec3 getSpecularColor();

        /**
         * @brief Returns the Phong shininess exponent.
         * @return Shininess value.
         */
        float getShininess();

        /**
         * @brief Returns the PBR metallic factor.
         * @return Metallic value.
         */
        float getMetallic();

        /**
         * @brief Returns the PBR roughness factor.
         * @return Roughness value.
         */
        float getRoughness();

        /**
         * @brief Configures face culling for this material.
         * @param newSingleSided true to enable face culling.
         * @param newCullBack    true to cull back faces; false to cull front faces.
         */
        void setSingleSided(bool newSingleSided, bool newCullBack = true);

        /**
         * @brief Returns whether face culling is enabled.
         * @return true if single-sided rendering is active.
         */
        bool getSingleSided();

        /**
         * @brief Returns which face is culled when single-sided rendering is on.
         * @return true if back faces are culled; false if front faces are culled.
         */
        bool getCullBack();

    protected:
    private:
        kShader          *shader      = nullptr;         ///< Bound shader program.
        kTransparentType  transparent = TRANSP_TYPE_NONE; ///< Blend mode.
        std::vector<kTexture *> textures;                ///< Ordered texture list.

        kVec2 uvTiling = kVec2(1, 1); ///< UV tiling multiplier.

        kVec3  ambientColor  = kVec3(1.0f, 1.0f, 1.0f); ///< Ambient surface colour.
        kVec3  diffuseColor  = kVec3(1.0f, 1.0f, 1.0f); ///< Diffuse surface colour.
        kVec3  specularColor = kVec3(1.0f, 1.0f, 1.0f); ///< Specular surface colour.
        float shininess  = 32.0f; ///< Phong shininess exponent.
        float metallic   = 0.0f;  ///< PBR metallic factor.
        float roughness  = 0.5f;  ///< PBR roughness factor.

        bool isSingleSided = true; ///< Face culling enabled.
        bool isCullBack    = true; ///< Cull back (true) or front (false) faces.
    };
}

#endif // KMATERIAL_H
