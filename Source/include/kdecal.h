/**
 * @file kdecal.h
 * @brief Flat sticker-quad scene node (decal).
 *
 * A decal is a thin, flat, textured quad that is placed slightly off a surface
 * (a wall, the floor, etc.) and rendered transform-oriented with its material,
 * so it behaves like a sticker.  Position/rotation/scale on the kObject
 * transform position the quad, orient it against the surface and size it.
 *
 * Two decal-specific properties are stored on the node:
 *  - surface offset: how far the quad floats above its pivot (along its local
 *    +Y / face normal) so it does not z-fight with the surface underneath;
 *  - shader type ("flat", "pbr" or "phong"): the built-in material that is
 *    applied to a freshly created decal. A separately assigned .mat asset (via
 *    the material UUID inherited from kObject) overrides this at render time.
 *
 * Rendering uses the same material pipeline as kMesh: assign a material whose
 * albedo map carries the decal artwork. "Flat" is unlit; "PBR" and "Phong" are
 * lit by the scene lights.
 */
#ifndef KDECAL_H
#define KDECAL_H

#include "kexport.h"
#include "kdriver.h"

#include "kobject.h"

namespace kemena
{
    /**
     * @brief Scene-graph node that draws a flat 1x1 unit quad (XZ plane,
     *        normal +Y) stamped with its material.
     *
     * The quad geometry is created lazily on the first draw() call and
     * released in the destructor.  The transform, material UUID, shader type
     * and surface offset are serialised; the geometry is always rebuilt.
     */
    class KEMENA3D_API kDecal : public kObject
    {
    public:
        /**
         * @brief Constructs a decal node and optionally attaches it to a parent.
         * @param parentNode Parent scene-graph node, or nullptr for a root node.
         */
        kDecal(kObject *parentNode = nullptr);

        /** @brief Destroys the decal and releases its GPU quad buffers. */
        ~kDecal();

        /**
         * @brief Draws the flat quad.
         *
         * Assumes the caller (kRenderer) has already bound a shader and set the
         * transform/material uniforms for this object, mirroring how kMesh nodes
         * are drawn.
         */
        void draw() override;

        /**
         * @brief Returns the decal shader type marker.
         * @return "flat", "pbr" or "phong" (used to rebuild the default material).
         */
        kString getShaderType() const;

        /**
         * @brief Sets the decal shader type marker.
         *
         * This only records the choice that should drive the built-in default
         * material; it does not rebuild the runtime material here (the editor
         * owns material construction).
         * @param type "flat", "pbr" or "phong".
         */
        void setShaderType(const kString &type);

        /**
         * @brief Returns how far the quad floats above its pivot, along its
         *        local +Y (face normal).
         * @return Surface offset in local units.
         */
        float getSurfaceOffset() const;

        /**
         * @brief Sets how far the quad floats above its pivot.
         * @param offset New surface offset (0 to lie on the pivot plane).
         */
        void setSurfaceOffset(float offset);

        /**
         * @brief Serialises this decal node to JSON.
         *
         * Delegates to kObject::serialize() (transform, children, material UUID,
         * components), stamps the node type as "decal", and stores the shader
         * type and surface offset.
         * @return JSON object describing the decal.
         */
        json serialize() override;

    private:
        /// Allocates the unit-quad VAO/VBO/index buffers (idempotent).
        void buildQuad();
        /// (Re)uploads the position buffer when the surface offset changed.
        void refreshGeometry();

        uint32_t quadVAO  = 0; ///< Vertex array for the flat quad.
        uint32_t quadVBO  = 0; ///< Vertex position buffer.
        uint32_t quadUVBO = 0; ///< Vertex UV buffer.
        uint32_t quadNBO  = 0; ///< Vertex normal buffer (for lit materials).
        uint32_t quadEBO  = 0; ///< Index buffer.

        float    builtOffset    = 0.0f;  ///< Surface offset the buffers were built with.
        float    surfaceOffset  = 0.01f; ///< Quad elevation above the pivot (avoids z-fighting).
        kString  decalShaderType = "flat"; ///< Built-in shader choice ("flat"/"pbr"/"phong").
    };
}

#endif // KDECAL_H
