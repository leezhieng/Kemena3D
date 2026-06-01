#ifndef KMESHGENERATOR_H
#define KMESHGENERATOR_H

#include "kexport.h"
#include "kmesh.h"

namespace kemena
{
    /**
     * @brief Procedural mesh generators — no file I/O, no asset manager needed.
     *
     * All methods return a heap-allocated kMesh with vertex data already uploaded
     * to the GPU (generateVbo() has been called). The caller is responsible for
     * adding the mesh to a scene and assigning a material.
     */
    class KEMENA3D_API kMeshGenerator
    {
    public:
        /**
         * @brief Generate a flat quad in the XZ plane, facing +Y.
         * @param size Edge length of the quad along both X and Z (defaults to a 2×2 quad).
         * @return Heap-allocated, GPU-uploaded kMesh; caller takes ownership.
         */
        static kMesh *generatePlane(float size = 2.0f);

        /**
         * @brief Generate an axis-aligned cube centred at the origin.
         * @param size Edge length of the cube along each axis.
         * @return Heap-allocated, GPU-uploaded kMesh; caller takes ownership.
         */
        static kMesh *generateCube(float size = 2.0f);

        /**
         * @brief Generate a UV sphere centred at the origin.
         * @param radius Sphere radius.
         * @param stacks Number of horizontal subdivisions (latitude bands).
         * @param slices Number of vertical subdivisions (longitude segments).
         * @return Heap-allocated, GPU-uploaded kMesh; caller takes ownership.
         */
        static kMesh *generateSphere(float radius = 1.0f, int stacks = 18, int slices = 36);

        /**
         * @brief Generate an upright cylinder with flat top and bottom caps.
         * @param radius Radius of the cylinder.
         * @param height Height of the cylindrical body along the Y axis.
         * @param slices Number of radial subdivisions around the circumference.
         * @return Heap-allocated, GPU-uploaded kMesh; caller takes ownership.
         */
        static kMesh *generateCylinder(float radius = 1.0f, float height = 2.0f, int slices = 36);

        /**
         * @brief Generate a capsule: a cylinder closed with hemispherical caps.
         *
         * The total height of the resulting mesh is height + 2*radius.
         * @param radius Radius of the cylindrical body and hemispherical caps.
         * @param height Height of the straight cylindrical section (excluding caps).
         * @param slices Number of radial subdivisions around the circumference.
         * @param hemiStacks Number of stacks per hemispherical cap.
         * @return Heap-allocated, GPU-uploaded kMesh; caller takes ownership.
         */
        static kMesh *generateCapsule(float radius = 1.0f, float height = 2.0f,
                                      int slices = 36, int hemiStacks = 8);
    };
}

#endif // KMESHGENERATOR_H
