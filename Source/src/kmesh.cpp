#include "kmesh.h"

namespace kemena
{
    kMesh::kMesh(kObject *parentNode)
    {
        if (parentNode != nullptr)
            setParent(parentNode);
        setType(kNodeType::NODE_TYPE_MESH);

        calculateNormalMatrix();
    }

    kMesh::~kMesh()
    {
        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr) return;

        if (vao)               driver->deleteVertexArray(vao);
        if (indicesEbo)        driver->deleteBuffer(indicesEbo);
        if (vertexBuffer)      driver->deleteBuffer(vertexBuffer);
        if (vertexColorBuffer) driver->deleteBuffer(vertexColorBuffer);
        if (uvBuffer)          driver->deleteBuffer(uvBuffer);
        if (normalBuffer)      driver->deleteBuffer(normalBuffer);
        if (tangentBuffer)     driver->deleteBuffer(tangentBuffer);
        if (bitangentBuffer)   driver->deleteBuffer(bitangentBuffer);
        if (boneIDBuffer)      driver->deleteBuffer(boneIDBuffer);
        if (weightBuffer)      driver->deleteBuffer(weightBuffer);
    }

    void kMesh::setLoaded(bool newLoaded)
    {
        loaded = newLoaded;
    }

    bool kMesh::getLoaded()
    {
        return loaded;
    }

    void kMesh::setFileName(kString newFileName)
    {
        fileName = newFileName;
    }

    kString kMesh::getFileName()
    {
        return fileName;
    }

    void kMesh::setRefName(kString newRefName)
    {
        refName = newRefName;
    }

    kString kMesh::getRefName()
    {
        return refName;
    }

    void kMesh::setPosition(kVec3 newPosition)
    {
        kObject::setPosition(newPosition);
        calculateNormalMatrix();
    }

    void kMesh::setRotation(kQuat newRotation)
    {
        kObject::setRotation(newRotation);
        calculateNormalMatrix();
    }

    void kMesh::setScale(kVec3 newScale)
    {
        kObject::setScale(newScale);
        calculateNormalMatrix();
    }

    void kMesh::reserveBoneData(size_t vertexCount)
    {
        // Reserve space for bone IDs and weights
        boneIDs.reserve(vertexCount);
        weights.reserve(vertexCount);
    }

    void kMesh::reserveSpace(size_t vertexCount)
    {
        // Reserve space for vertices, normals, and UVs
        vertices.reserve(vertexCount);
        normals.reserve(vertexCount);
        uvs.reserve(vertexCount);

        // Reserve space for bone IDs and weights
        boneIDs.reserve(vertexCount);
        weights.reserve(vertexCount);

        // Reserve space for indices (if needed)
        indices.reserve(vertexCount * 3); // Assuming 3 indices per face (triangles)
    }

    void kMesh::setBoneInfoMap(std::map<kString, kBoneInfo> newBoneInfoMap)
    {
        boneInfoMap = newBoneInfoMap;
    }

    std::map<kString, kBoneInfo> &kMesh::getBoneInfoMap()
    {
        return boneInfoMap;
    }

    int &kMesh::getBoneCount()
    {
        return boneCount;
    }

    void kMesh::setBoneCount(int newBoneCount)
    {
        boneCount = newBoneCount;
    }

    void kMesh::addIndex(uint32_t index)
    {
        indices.push_back(index);
    }

    std::vector<uint32_t> kMesh::getIndices()
    {
        return indices;
    }

    void kMesh::addVertex(kVec3 vertex)
    {
        vertices.push_back(vertex);
    }

    std::vector<kVec3> kMesh::getVertices()
    {
        return vertices;
    }

    void kMesh::addUV(kVec2 uv)
    {
        uvs.push_back(uv);
    }

    std::vector<kVec2> kMesh::getUVs()
    {
        return uvs;
    }

    void kMesh::addVertexColor(kVec3 color)
    {
        vertexColors.push_back(color);
    }

    std::vector<kVec3> kMesh::getVertexColors()
    {
        return vertexColors;
    }

    void kMesh::addNormal(kVec3 normal)
    {
        normals.push_back(normal);
    }

    std::vector<kVec3> kMesh::getNormals()
    {
        return normals;
    }

    void kMesh::addTangent(kVec3 tangent)
    {
        tangents.push_back(tangent);
    }

    std::vector<kVec3> kMesh::getTangents()
    {
        return tangents;
    }

    void kMesh::addBitangent(kVec3 bitangent)
    {
        bitangents.push_back(bitangent);
    }

    std::vector<kVec3> kMesh::getBitangents()
    {
        return bitangents;
    }

    void kMesh::addBoneID(const kIvec4 &boneID)
    {
        boneIDs.push_back(boneID);
    }

    void kMesh::setBoneID(size_t vertexIndex, const kIvec4 &boneID)
    {
        if (vertexIndex < boneIDs.size())
        {
            boneIDs[vertexIndex] = boneID;
        }
        else
        {
            throw std::out_of_range("Vertex index out of range in getBoneID.");
        }
    }

    kIvec4 kMesh::getBoneID(size_t vertexIndex)
    {
        if (vertexIndex < boneIDs.size())
        {
            return boneIDs[vertexIndex];
        }
        else
        {
            throw std::out_of_range("Vertex index out of range in getBoneID.");
        }
    }

    std::vector<kIvec4> kMesh::getBoneIDs()
    {
        return boneIDs;
    }

    void kMesh::setBoneIDs(std::vector<kIvec4> newBoneIDs)
    {
        boneIDs = newBoneIDs;
    }

    void kMesh::addWeight(const kVec4 &weight)
    {
        weights.push_back(weight);
    }

    void kMesh::setWeight(size_t vertexIndex, const kVec4 &weight)
    {
        if (vertexIndex < weights.size())
        {
            weights[vertexIndex] = weight;
        }
        else
        {
            throw std::out_of_range("Vertex index out of range in getWeight.");
        }
    }

    kVec4 kMesh::getWeight(size_t vertexIndex)
    {
        if (vertexIndex < weights.size())
        {
            return weights[vertexIndex];
        }
        else
        {
            throw std::out_of_range("Vertex index out of range in getWeight.");
        }
    }

    std::vector<kVec4> kMesh::getWeights()
    {
        return weights;
    }

    void kMesh::setWeights(std::vector<kVec4> newWeights)
    {
        weights = newWeights;
    }

    int kMesh::getVertexCount()
    {
        return vertices.size();
    }

    uint32_t kMesh::getVertexArrayObject()
    {
        return vao;
    }

    uint32_t kMesh::getVertexBuffer()
    {
        return vertexBuffer;
    }

    uint32_t kMesh::getVertexColorBuffer()
    {
        return vertexColorBuffer;
    }

    void kMesh::setNormalMatrix(kMat4 newNormalMatrix)
    {
        normalMatrix = newNormalMatrix;
    }

    kMat4 kMesh::getNormalMatrix()
    {
        return normalMatrix;
    }

    void kMesh::generateTangents()
    {
        if (vertices.empty() || uvs.empty() || indices.empty()) return;

        tangents.assign(vertices.size(), kVec3(0.0f));
        bitangents.assign(vertices.size(), kVec3(0.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
            kVec3 edge1  = vertices[i1] - vertices[i0];
            kVec3 edge2  = vertices[i2] - vertices[i0];
            kVec2 dUV1   = uvs[i1] - uvs[i0];
            kVec2 dUV2   = uvs[i2] - uvs[i0];

            float denom = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
            if (std::abs(denom) < 1e-6f) continue;
            float f = 1.0f / denom;

            kVec3 t = f * (dUV2.y * edge1 - dUV1.y * edge2);
            kVec3 b = f * (-dUV2.x * edge1 + dUV1.x * edge2);

            tangents[i0]   += t; tangents[i1]   += t; tangents[i2]   += t;
            bitangents[i0] += b; bitangents[i1] += b; bitangents[i2] += b;
        }

        for (size_t i = 0; i < tangents.size(); ++i)
        {
            float tlen = glm::length(tangents[i]);
            float blen = glm::length(bitangents[i]);
            if (tlen > 1e-6f) tangents[i]   = tangents[i] / tlen;
            if (blen > 1e-6f) bitangents[i] = bitangents[i] / blen;
        }
    }

    void kMesh::generateVbo()
    {
        assert(vertices.size() == normals.size());
        assert(vertices.size() == uvs.size());

        // Auto-fill default bone data so non-skinned meshes don't pick up
        // OpenGL's generic attribute defaults (weights[3] = 1.0 would cause
        // a spurious bone-0 transform to be applied).
        if (boneIDs.empty() && !vertices.empty())
            boneIDs.assign(vertices.size(), kIvec4(-1, -1, -1, -1));
        if (weights.empty() && !vertices.empty())
            weights.assign(vertices.size(), kVec4(0.0f, 0.0f, 0.0f, 0.0f));

        assert(vertices.size() == boneIDs.size());
        assert(vertices.size() == weights.size());

        kDriver *driver = kDriver::getCurrent();

        vao = driver->createVertexArray();
        driver->bindVertexArray(vao);

        // Index buffer
        if (!indices.empty())
        {
            indicesEbo = driver->createBuffer();
            driver->uploadIndexBuffer(indicesEbo, indices.data(), indices.size() * sizeof(uint32_t));
        }

        // Vertex position (location 0)
        if (!vertices.empty())
        {
            vertexBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(kVec3));
            driver->setVertexAttribFloat(0, 3, sizeof(kVec3), 0);
        }

        // Vertex color (location 1)
        if (!vertexColors.empty())
        {
            vertexColorBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(vertexColorBuffer, vertexColors.data(), vertexColors.size() * sizeof(kVec3));
            driver->setVertexAttribFloat(1, 3, sizeof(kVec3), 0);
        }

        // UV (location 2)
        if (!uvs.empty())
        {
            uvBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(uvBuffer, uvs.data(), uvs.size() * sizeof(kVec2));
            driver->setVertexAttribFloat(2, 2, sizeof(kVec2), 0);
        }

        // Normals (location 3)
        if (!normals.empty())
        {
            normalBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(normalBuffer, normals.data(), normals.size() * sizeof(kVec3));
            driver->setVertexAttribFloat(3, 3, sizeof(kVec3), 0);
        }

        // Tangents (location 4)
        if (!tangents.empty())
        {
            tangentBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(tangentBuffer, tangents.data(), tangents.size() * sizeof(kVec3));
            driver->setVertexAttribFloat(4, 3, sizeof(kVec3), 0);
        }

        // Bitangents (location 5)
        if (!bitangents.empty())
        {
            bitangentBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(bitangentBuffer, bitangents.data(), bitangents.size() * sizeof(kVec3));
            driver->setVertexAttribFloat(5, 3, sizeof(kVec3), 0);
        }

        // Bone IDs (location 6) — integer attrib
        if (!boneIDs.empty())
        {
            boneIDBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(boneIDBuffer, boneIDs.data(), boneIDs.size() * sizeof(kIvec4));
            driver->setVertexAttribInt(6, 4, sizeof(kIvec4), 0);
        }

        // Weights (location 7)
        if (!weights.empty())
        {
            weightBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(weightBuffer, weights.data(), weights.size() * sizeof(kVec4));
            driver->setVertexAttribFloat(7, 4, sizeof(kVec4), 0);
        }

        driver->unbindVertexArray();

        computeLocalAABB();
        calculateModelMatrix();
        calculateNormalMatrix();
    }

    void kMesh::computeLocalAABB()
    {
        localAABB = kAABB(); // reset to invalid state
        for (const kVec3 &v : vertices)
            localAABB.expandBy(v);
    }

    kAABB kMesh::getLocalAABB() const
    {
        return localAABB;
    }

    kAABB kMesh::getWorldAABB()
    {
        if (!localAABB.isValid())
            return kAABB(getPosition(), getPosition());

        kMat4 world = getModelMatrixWorld();

        // Transform all 8 corners and compute enclosing AABB
        kVec3 corners[8] = {
            {localAABB.min.x, localAABB.min.y, localAABB.min.z},
            {localAABB.max.x, localAABB.min.y, localAABB.min.z},
            {localAABB.min.x, localAABB.max.y, localAABB.min.z},
            {localAABB.max.x, localAABB.max.y, localAABB.min.z},
            {localAABB.min.x, localAABB.min.y, localAABB.max.z},
            {localAABB.max.x, localAABB.min.y, localAABB.max.z},
            {localAABB.min.x, localAABB.max.y, localAABB.max.z},
            {localAABB.max.x, localAABB.max.y, localAABB.max.z},
        };

        kAABB result;
        for (const kVec3 &c : corners)
            result.expandBy(kVec3(world * kVec4(c, 1.0f)));
        return result;
    }

    void kMesh::calculateNormalMatrix()
    {
        setNormalMatrix(glm::transpose(glm::inverse(getModelMatrixWorld())));
    }

    void kMesh::draw()
    {
        if (vao == 0 || indices.empty())
            return;

        kDriver::getCurrent()->drawIndexed(vao, (int)indices.size());
    }

    json kMesh::serialize()
    {
        // Delegate to the base so transform, children, scripts (full format),
        // physics, character controller and navigation components are all
        // emitted consistently, then add mesh-specific fields on top.
        json data = kObject::serialize();
        data["type"]           = "mesh";
        data["visible"]        = getVisible();
        data["static"]         = getStatic();
        data["file_name"]      = getFileName();
        data["reference"]      = getRefName();
        data["cast_shadow"]    = getCastShadow();
        data["receive_shadow"] = getReceiveShadow();
        return data;
    }

    void kMesh::deserialize(json data)
    {
    }

    void kMesh::setVisible(bool newVisible)
    {
        isVisible = newVisible;
    }

    bool kMesh::getVisible()
    {
        return isVisible;
    }

    void kMesh::setCastShadow(bool newCastShadow)
    {
        isCastShadow = newCastShadow;
    }

    bool kMesh::getCastShadow()
    {
        return isCastShadow;
    }

    void kMesh::setReceiveShadow(bool newReceiveShadow)
    {
        isReceiveShadow = newReceiveShadow;
    }

    bool kMesh::getReceiveShadow()
    {
        return isReceiveShadow;
    }

    void kMesh::setVertexBoneData(size_t vertexID, int boneID, float weight)
    {
        if (vertexID >= boneIDs.size() || vertexID >= weights.size())
        {
            throw std::out_of_range("Vertex ID out of range in setVertexBoneData.");
        }

        // Find the first empty slot in the bone IDs and weights for this vertex
        for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
        {
            if (boneIDs[vertexID][i] == -1)
            {
                boneIDs[vertexID][i] = boneID;
                weights[vertexID][i] = weight;
                return;
            }
        }

        // If no empty slot is found, throw an error
        throw std::runtime_error("Vertex already reached maximum bone influences.");
    }

    void kMesh::setAnimator(kAnimator *newAnimator)
    {
        animator = newAnimator;
        setSkinned(true);

        if (getChildren().size() > 0)
        {
            for (size_t i = 0; i < getChildren().size(); ++i)
            {
                if (getChildren().at(i)->getType() == NODE_TYPE_MESH)
                {
                    kMesh *childMesh = (kMesh *)getChildren().at(i);

                    // childMesh->setAnimator(animator);
                    childMesh->setSkinned(true);
                }
            }
        }
    }

    kAnimator *kMesh::getAnimator()
    {
        return animator;
    }

    void kMesh::setSkinned(bool newSkinned)
    {
        isSkinned = newSkinned;
    }

    bool kMesh::getSkinned()
    {
        return isSkinned;
    }
}
