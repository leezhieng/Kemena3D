/**
 * @file kassimp_internal.h
 * @brief Private SDK-internal conversions between Assimp and GLM types.
 *
 * NOT part of the public API — this header is intentionally located under
 * Source/src so it isn't installed alongside the public Kemena3D headers.
 * Include it only from import-side .cpp files where Assimp is already part
 * of the implementation (kassetmanager.cpp, kskelanimation.cpp, kbone.cpp).
 */

#ifndef KASSIMP_INTERNAL_H
#define KASSIMP_INTERNAL_H

#include "kdatatype.h"

#include <assimp/quaternion.h>
#include <assimp/vector3.h>
#include <assimp/matrix4x4.h>

namespace kemena
{
    namespace kAssimpInternal
    {
        /** @brief Converts an Assimp 4x4 matrix to a GLM kMat4 (column-major). */
        inline kMat4 toMat4(const aiMatrix4x4 &from)
        {
            // Assimp uses (a,b,c,d) as rows; GLM expects columns.
            return kMat4(from.a1, from.b1, from.c1, from.d1,
                         from.a2, from.b2, from.c2, from.d2,
                         from.a3, from.b3, from.c3, from.d3,
                         from.a4, from.b4, from.c4, from.d4);
        }

        /** @brief Converts an aiVector3D to a kVec2 (drops Z). */
        inline kVec2 toVec2(const aiVector3D &v)
        {
            return kVec2(v.x, v.y);
        }

        /** @brief Converts an aiVector3D to a kVec3. */
        inline kVec3 toVec3(const aiVector3D &v)
        {
            return kVec3(v.x, v.y, v.z);
        }

        /** @brief Converts an aiQuaternion to a kQuat. */
        inline kQuat toQuat(const aiQuaternion &q)
        {
            return kQuat(q.w, q.x, q.y, q.z);
        }
    }
}

#endif // KASSIMP_INTERNAL_H
