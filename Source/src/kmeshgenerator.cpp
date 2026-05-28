#include "kmeshgenerator.h"
#include <cmath>

namespace kemena
{
    static constexpr float K_PI = 3.14159265358979323846f;

    // -----------------------------------------------------------------------
    // Plane  —  1×1 quad in XZ, normal = +Y
    // -----------------------------------------------------------------------
    kMesh *kMeshGenerator::generatePlane(float size)
    {
        kMesh *mesh = new kMesh();
        mesh->setPrimitiveType("plane");
        float h = size * 0.5f;

        //  v2 ---- v3
        //  |  \     |
        //  |    \   |
        //  v0 ---- v1
        mesh->addVertex(kVec3(-h, 0.0f, -h));
        mesh->addVertex(kVec3( h, 0.0f, -h));
        mesh->addVertex(kVec3(-h, 0.0f,  h));
        mesh->addVertex(kVec3( h, 0.0f,  h));

        for (int i = 0; i < 4; i++)
            mesh->addNormal(kVec3(0.0f, 1.0f, 0.0f));

        mesh->addUV(kVec2(0.0f, 0.0f));
        mesh->addUV(kVec2(1.0f, 0.0f));
        mesh->addUV(kVec2(0.0f, 1.0f));
        mesh->addUV(kVec2(1.0f, 1.0f));

        mesh->addIndex(0); mesh->addIndex(2); mesh->addIndex(1);
        mesh->addIndex(1); mesh->addIndex(2); mesh->addIndex(3);

        mesh->generateTangents();
        mesh->generateVbo();
        mesh->setLoaded(true);
        return mesh;
    }

    // -----------------------------------------------------------------------
    // Cube  —  axis-aligned unit cube, 24 hard-normal vertices, 36 indices
    // -----------------------------------------------------------------------
    kMesh *kMeshGenerator::generateCube(float size)
    {
        kMesh *mesh = new kMesh();
        mesh->setPrimitiveType("cube");
        float h = size * 0.5f;

        struct Face { kVec3 n, r, u; };
        static const Face faces[6] = {
            { kVec3( 0, 0, 1), kVec3( 1, 0, 0), kVec3(0, 1, 0) }, // +Z front
            { kVec3( 0, 0,-1), kVec3(-1, 0, 0), kVec3(0, 1, 0) }, // -Z back
            { kVec3(-1, 0, 0), kVec3( 0, 0, 1), kVec3(0, 1, 0) }, // -X left
            { kVec3( 1, 0, 0), kVec3( 0, 0,-1), kVec3(0, 1, 0) }, // +X right
            { kVec3( 0, 1, 0), kVec3( 1, 0, 0), kVec3(0, 0,-1) }, // +Y top
            { kVec3( 0,-1, 0), kVec3( 1, 0, 0), kVec3(0, 0, 1) }, // -Y bottom
        };
        static const kVec2 uvs[4] = {
            kVec2(0,0), kVec2(1,0), kVec2(0,1), kVec2(1,1)
        };

        for (int f = 0; f < 6; f++)
        {
            kVec3 c = faces[f].n * h;
            mesh->addVertex(c - faces[f].r * h - faces[f].u * h);
            mesh->addVertex(c + faces[f].r * h - faces[f].u * h);
            mesh->addVertex(c - faces[f].r * h + faces[f].u * h);
            mesh->addVertex(c + faces[f].r * h + faces[f].u * h);

            for (int i = 0; i < 4; i++) mesh->addNormal(faces[f].n);
            for (int i = 0; i < 4; i++) mesh->addUV(uvs[i]);

            uint32_t b = (uint32_t)(f * 4);
            mesh->addIndex(b+0); mesh->addIndex(b+1); mesh->addIndex(b+2);
            mesh->addIndex(b+1); mesh->addIndex(b+3); mesh->addIndex(b+2);
        }

        mesh->generateTangents();
        mesh->generateVbo();
        mesh->setLoaded(true);
        return mesh;
    }

    // -----------------------------------------------------------------------
    // Sphere  —  UV sphere
    // -----------------------------------------------------------------------
    kMesh *kMeshGenerator::generateSphere(float radius, int stacks, int slices)
    {
        kMesh *mesh = new kMesh();
        mesh->setPrimitiveType("sphere");

        for (int s = 0; s <= stacks; s++)
        {
            float phi = K_PI * s / stacks;
            float y   = std::cos(phi);
            float r   = std::sin(phi);

            for (int l = 0; l <= slices; l++)
            {
                float theta = 2.0f * K_PI * l / slices;
                float nx = r * std::cos(theta);
                float ny = y;
                float nz = r * std::sin(theta);

                mesh->addVertex(kVec3(nx * radius, ny * radius, nz * radius));
                mesh->addNormal(kVec3(nx, ny, nz));
                mesh->addUV(kVec2((float)l / slices, (float)s / stacks));
            }
        }

        for (int s = 0; s < stacks; s++)
        {
            for (int l = 0; l < slices; l++)
            {
                uint32_t a = (uint32_t)(s * (slices+1) + l);
                uint32_t b = a + 1;
                uint32_t c = (uint32_t)((s+1) * (slices+1) + l);
                uint32_t d = c + 1;

                mesh->addIndex(a); mesh->addIndex(b); mesh->addIndex(c);
                mesh->addIndex(b); mesh->addIndex(d); mesh->addIndex(c);
            }
        }

        mesh->generateTangents();
        mesh->generateVbo();
        mesh->setLoaded(true);
        return mesh;
    }

    // -----------------------------------------------------------------------
    // Cylinder  —  side strip + top/bottom caps
    // -----------------------------------------------------------------------
    kMesh *kMeshGenerator::generateCylinder(float radius, float height, int slices)
    {
        kMesh *mesh = new kMesh();
        mesh->setPrimitiveType("cylinder");
        float halfH = height * 0.5f;

        // Side: pairs (bottom, top) per slice column
        for (int i = 0; i <= slices; i++)
        {
            float theta = 2.0f * K_PI * i / slices;
            float nx = std::cos(theta);
            float nz = std::sin(theta);
            float u  = (float)i / slices;

            mesh->addVertex(kVec3(nx * radius, -halfH, nz * radius));
            mesh->addNormal(kVec3(nx, 0.0f, nz));
            mesh->addUV(kVec2(u, 0.0f));

            mesh->addVertex(kVec3(nx * radius, halfH, nz * radius));
            mesh->addNormal(kVec3(nx, 0.0f, nz));
            mesh->addUV(kVec2(u, 1.0f));
        }
        for (int i = 0; i < slices; i++)
        {
            uint32_t b0 = (uint32_t)(i * 2);
            uint32_t t0 = b0 + 1;
            uint32_t b1 = b0 + 2;
            uint32_t t1 = b0 + 3;
            mesh->addIndex(b0); mesh->addIndex(t0); mesh->addIndex(b1);
            mesh->addIndex(t0); mesh->addIndex(t1); mesh->addIndex(b1);
        }

        // Cap helper: adds center + rim verts and fan indices
        auto addCap = [&](float y, float normalY, bool flipWinding)
        {
            uint32_t centerIdx = (uint32_t)mesh->getVertexCount();
            mesh->addVertex(kVec3(0.0f, y, 0.0f));
            mesh->addNormal(kVec3(0.0f, normalY, 0.0f));
            mesh->addUV(kVec2(0.5f, 0.5f));

            for (int i = 0; i <= slices; i++)
            {
                float theta = 2.0f * K_PI * i / slices;
                float cx = std::cos(theta);
                float cz = std::sin(theta);
                mesh->addVertex(kVec3(cx * radius, y, cz * radius));
                mesh->addNormal(kVec3(0.0f, normalY, 0.0f));
                mesh->addUV(kVec2(cx * 0.5f + 0.5f, cz * 0.5f + 0.5f));
            }

            for (int i = 0; i < slices; i++)
            {
                uint32_t a = centerIdx + 1 + (uint32_t)i;
                uint32_t b = a + 1;
                if (flipWinding) { mesh->addIndex(centerIdx); mesh->addIndex(b); mesh->addIndex(a); }
                else             { mesh->addIndex(centerIdx); mesh->addIndex(a); mesh->addIndex(b); }
            }
        };

        addCap(-halfH, -1.0f, false); // bottom — (center,a,b) cross product → -Y ✓
        addCap( halfH,  1.0f, true);  // top   — needs flip for (center,b,a) → +Y ✓

        mesh->generateTangents();
        mesh->generateVbo();
        mesh->setLoaded(true);
        return mesh;
    }

    // -----------------------------------------------------------------------
    // Capsule  —  hemisphere rings separated by a cylinder wall
    //
    // Ring layout (top → bottom):
    //   0 .. hemiStacks        : top hemisphere  (phi 0 → π/2, offset +halfH)
    //   hemiStacks+1 .. 2*hemiStacks+1 : bottom hemisphere (phi π/2 → π, offset -halfH)
    //
    // The quads between ring[hemiStacks] and ring[hemiStacks+1] form the
    // cylinder wall.  Total cylinder length = height.
    // -----------------------------------------------------------------------
    kMesh *kMeshGenerator::generateCapsule(float radius, float height,
                                           int slices, int hemiStacks)
    {
        kMesh *mesh = new kMesh();
        mesh->setPrimitiveType("capsule");
        float halfH = height * 0.5f;

        int totalRings = 2 * hemiStacks + 2; // two equator rings (top + bottom)

        for (int s = 0; s < totalRings; s++)
        {
            float phi, yOffset;
            if (s <= hemiStacks)
            {
                phi     = (K_PI * 0.5f) * s / hemiStacks; // 0 → π/2
                yOffset = halfH;
            }
            else
            {
                int bs  = s - hemiStacks - 1;              // 0 → hemiStacks
                phi     = (K_PI * 0.5f) + (K_PI * 0.5f) * bs / hemiStacks; // π/2 → π
                yOffset = -halfH;
            }

            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            for (int l = 0; l <= slices; l++)
            {
                float theta = 2.0f * K_PI * l / slices;
                float nx = sinPhi * std::cos(theta);
                float ny = cosPhi;
                float nz = sinPhi * std::sin(theta);

                mesh->addVertex(kVec3(nx * radius, yOffset + ny * radius, nz * radius));
                mesh->addNormal(kVec3(nx, ny, nz));
                mesh->addUV(kVec2((float)l / slices, (float)s / (totalRings - 1)));
            }
        }

        for (int s = 0; s < totalRings - 1; s++)
        {
            for (int l = 0; l < slices; l++)
            {
                uint32_t a = (uint32_t)(s * (slices+1) + l);
                uint32_t b = a + 1;
                uint32_t c = (uint32_t)((s+1) * (slices+1) + l);
                uint32_t d = c + 1;

                mesh->addIndex(a); mesh->addIndex(b); mesh->addIndex(c);
                mesh->addIndex(b); mesh->addIndex(d); mesh->addIndex(c);
            }
        }

        mesh->generateTangents();
        mesh->generateVbo();
        mesh->setLoaded(true);
        return mesh;
    }
}
