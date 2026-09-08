#include "kdecal.h"
#include "kdatatype.h"

namespace kemena
{
    kDecal::kDecal(kObject *parentNode)
    {
        if (parentNode != nullptr)
            setParent(parentNode);
        setType(kNodeType::NODE_TYPE_DECAL);
    }

    kDecal::~kDecal()
    {
        if (kDriver *driver = kDriver::getCurrent())
        {
            if (quadEBO)  driver->deleteBuffer(quadEBO);
            if (quadNBO)  driver->deleteBuffer(quadNBO);
            if (quadUVBO) driver->deleteBuffer(quadUVBO);
            if (quadVBO)  driver->deleteBuffer(quadVBO);
            if (quadVAO)  driver->deleteVertexArray(quadVAO);
        }
        quadVAO = quadVBO = quadUVBO = quadNBO = quadEBO = 0;
    }

    void kDecal::buildQuad()
    {
        if (quadVAO != 0)
            return;

        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr)
            return;

        // Flat 1x1 quad in the XZ plane, normal +Y, exactly like the engine's
        // "plane" primitive (kMeshGenerator::generatePlane).  A freshly created
        // decal therefore lies flat on the floor; rotate it -90 degrees around
        // X to stand it upright against a wall.
        //
        // The whole plane is lifted by `surfaceOffset` along +Y so the sticker
        // hovers just above the surface and does not z-fight with it.
        const float y = surfaceOffset;
        const float positions[12] =
        {
            -0.5f, y, -0.5f,
             0.5f, y, -0.5f,
            -0.5f, y,  0.5f,
             0.5f, y,  0.5f,
        };
        const float uvs[8] =
        {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 1.0f,
        };
        const float normals[12] =
        {
            0.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
        };
        const uint32_t indices[6] = { 0, 2, 1, 1, 2, 3 };

        // Vertex attribute layout mirrors kMesh::generateVbo so the same
        // material shaders (and the picking shader) can render the quad:
        //   loc0 = position, loc2 = uv, loc3 = normal.
        quadVAO = driver->createVertexArray();
        driver->bindVertexArray(quadVAO);

        quadVBO = driver->createBuffer();
        driver->uploadVertexBuffer(quadVBO, positions, sizeof(positions));
        driver->setVertexAttribFloat(0, 3, sizeof(kVec3), 0);

        quadUVBO = driver->createBuffer();
        driver->uploadVertexBuffer(quadUVBO, uvs, sizeof(uvs));
        driver->setVertexAttribFloat(2, 2, sizeof(kVec2), 0);

        quadNBO = driver->createBuffer();
        driver->uploadVertexBuffer(quadNBO, normals, sizeof(normals));
        driver->setVertexAttribFloat(3, 3, sizeof(kVec3), 0);

        quadEBO = driver->createBuffer();
        driver->uploadIndexBuffer(quadEBO, indices, sizeof(indices));

        driver->unbindVertexArray();

        builtOffset = surfaceOffset;
    }

    void kDecal::refreshGeometry()
    {
        if (quadVAO == 0 || quadVBO == 0)
            return;
        if (builtOffset == surfaceOffset)
            return;

        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr)
            return;

        const float y = surfaceOffset;
        const float positions[12] =
        {
            -0.5f, y, -0.5f,
             0.5f, y, -0.5f,
            -0.5f, y,  0.5f,
             0.5f, y,  0.5f,
        };
        driver->updateBufferSubData(quadVBO, positions, sizeof(positions), 0);
        builtOffset = surfaceOffset;
    }

    void kDecal::draw()
    {
        if (quadVAO == 0)
            buildQuad();
        else
            refreshGeometry();

        if (quadVAO == 0)
            return;

        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr)
            return;

        driver->drawIndexed(quadVAO, 6);
    }

    kString kDecal::getShaderType() const
    {
        return decalShaderType;
    }

    void kDecal::setShaderType(const kString &type)
    {
        decalShaderType = type;
    }

    float kDecal::getSurfaceOffset() const
    {
        return surfaceOffset;
    }

    void kDecal::setSurfaceOffset(float offset)
    {
        surfaceOffset = offset;
    }

    json kDecal::serialize()
    {
        // Delegate to the base so transform, children, scripts (full format),
        // material UUID, physics, character controller and navigation
        // components are all emitted consistently, then stamp the node type and
        // the decal-specific fields.
        json data = kObject::serialize();
        data["type"] = "decal";
        data["decal_offset"] = surfaceOffset;
        data["decal_shader"] = decalShaderType;
        return data;
    }
}
