/**
 * @file kmesh.h
 * @brief Polygonal mesh node with skeletal animation support.
 */

#ifndef KMESH_H
#define KMESH_H

#include "kexport.h"
#include "kdriver.h"

#include <map>
#include <vector>
#include <string>

#include "kobject.h"
#include "kbone.h"
// (Skeletal animation is reached through the forward-declared kAnimator
// pointer below — no animation header needed at the mesh-header level.)
#include "kanimator.h"

namespace kemena
{
    class kAnimator;

    /**
     * @brief Scene-graph node that holds renderable geometry.
     *
     * Stores per-vertex attributes (position, UV, normal, tangent, bitangent,
     * colour, bone IDs, bone weights) together with an index buffer.  GPU
     * buffers are allocated lazily via generateVbo().  Supports both static
     * and skeletal-animated meshes through an optional kAnimator attachment.
     */
    class KEMENA3D_API kMesh : public kObject
    {
    public:
        /**
         * @brief Constructs a mesh node and optionally attaches it to a parent.
         * @param parentNode Parent scene-graph node, or nullptr for a root node.
         */
        kMesh(kObject *parentNode = nullptr);
        ~kMesh();

        /**
         * @brief Marks whether the mesh geometry has been fully loaded.
         * @param newLoaded true once all vertex data has been populated.
         */
        void setLoaded(bool newLoaded);

        /**
         * @brief Returns whether the mesh geometry is fully loaded.
         * @return true after all vertex data has been populated.
         */
        bool getLoaded();

        /**
         * @brief Sets the source asset file path.
         * @param newFileName Path to the mesh asset on disk.
         */
        void setFileName(kString newFileName);

        /**
         * @brief Returns the source asset file path.
         * @return File path kString.
         */
        kString getFileName();

        /**
         * @brief Sets the reference name used to identify shared mesh data.
         * @param newRefName Reference identifier kString.
         */
        void setRefName(kString newRefName);

        /**
         * @brief Returns the reference name.
         * @return Reference identifier kString.
         */
        kString getRefName();

        /**
         * @brief Marks this mesh as one of the built-in procedural primitives
         *        ("cube", "sphere", "cylinder", "capsule", "plane"). Set by
         *        kMeshGenerator so save/load and duplicate can rebuild the
         *        geometry from the marker instead of needing a file on disk.
         */
        void    setPrimitiveType(kString type);
        kString getPrimitiveType() const;

        /** @brief Propagates a local position change to this mesh and its children. */
        void setPosition(kVec3 newPosition);
        /** @brief Propagates a local rotation change to this mesh and its children. */
        void setRotation(kQuat newRotation);
        /** @brief Propagates a local scale change to this mesh and its children. */
        void setScale(kVec3 newScale);

        /**
         * @brief Pre-allocates bone ID and weight arrays for a given vertex count.
         * @param vertexCount Number of vertices to reserve space for.
         */
        void reserveBoneData(size_t vertexCount);

        /**
         * @brief Pre-allocates all per-vertex attribute vectors.
         * @param vertexCount Number of vertices to reserve space for.
         */
        void reserveSpace(size_t vertexCount);

        /**
         * @brief Replaces the bone-name-to-info map.
         * @param newBoneInfoMap Map from bone name to kBoneInfo.
         */
        void setBoneInfoMap(std::map<kString, kBoneInfo> newBoneInfoMap);

        /**
         * @brief Returns a reference to the bone-name-to-info map.
         * @return Mutable reference to the internal map.
         */
        std::map<kString, kBoneInfo> &getBoneInfoMap();

        /**
         * @brief Returns a reference to the bone counter used during loading.
         * @return Mutable reference to the bone count integer.
         */
        int &getBoneCount();

        /**
         * @brief Sets the total number of bones.
         * @param newBoneCount Bone count.
         */
        void setBoneCount(int newBoneCount);

        /**
         * @brief Appends an index to the index buffer.
         * @param index Triangle vertex index.
         */
        void addIndex(uint32_t index);

        /**
         * @brief Returns a copy of the index buffer.
         * @return Vector of triangle indices.
         */
        std::vector<uint32_t> getIndices();

        /**
         * @brief Appends a vertex position.
         * @param vertex XYZ position in object space.
         */
        void addVertex(kVec3 vertex);

        /**
         * @brief Returns a copy of the vertex position buffer.
         * @return Vector of positions.
         */
        std::vector<kVec3> getVertices();

        /**
         * @brief Appends a UV coordinate.
         * @param uv Texture coordinate (U, V).
         */
        void addUV(kVec2 uv);

        /**
         * @brief Returns a copy of the UV coordinate buffer.
         * @return Vector of UV coordinates.
         */
        std::vector<kVec2> getUVs();

        /**
         * @brief Appends a per-vertex colour.
         * @param color RGB colour value (0..1 per channel).
         */
        void addVertexColor(kVec3 color);

        /**
         * @brief Returns a copy of the per-vertex colour buffer.
         * @return Vector of RGB colours.
         */
        std::vector<kVec3> getVertexColors();

        /**
         * @brief Appends a vertex normal.
         * @param normal Normalised surface normal in object space.
         */
        void addNormal(kVec3 normal);

        /**
         * @brief Returns a copy of the normal buffer.
         * @return Vector of normals.
         */
        std::vector<kVec3> getNormals();

        /**
         * @brief Appends a vertex tangent.
         * @param tangent Normalised tangent vector in object space.
         */
        void addTangent(kVec3 tangent);

        /**
         * @brief Returns a copy of the tangent buffer.
         * @return Vector of tangents.
         */
        std::vector<kVec3> getTangents();

        /**
         * @brief Appends a vertex bitangent.
         * @param bitangent Normalised bitangent vector in object space.
         */
        void addBitangent(kVec3 bitangent);

        /**
         * @brief Returns a copy of the bitangent buffer.
         * @return Vector of bitangents.
         */
        std::vector<kVec3> getBitangents();

        /**
         * @brief Appends a bone-ID tuple for the next vertex.
         * @param boneID Up to four bone indices influencing the vertex.
         */
        void addBoneID(const kIvec4 &boneID);

        /**
         * @brief Overwrites the bone-ID tuple for a specific vertex.
         * @param vertexIndex Zero-based vertex index.
         * @param boneID      New bone-ID tuple.
         */
        void setBoneID(size_t vertexIndex, const kIvec4 &boneID);

        /**
         * @brief Returns the bone-ID tuple for a vertex.
         * @param vertexIndex Zero-based vertex index.
         * @return Four bone indices.
         */
        kIvec4 getBoneID(size_t vertexIndex);

        /**
         * @brief Returns a copy of the bone-ID buffer.
         * @return Vector of kIvec4 bone indices, one per vertex.
         */
        std::vector<kIvec4> getBoneIDs();

        /**
         * @brief Replaces the entire bone-ID buffer.
         * @param newBoneIDs New buffer.
         */
        void setBoneIDs(std::vector<kIvec4> newBoneIDs);

        /**
         * @brief Appends a bone-weight tuple for the next vertex.
         * @param weight Four blend weights (must sum to 1.0).
         */
        void addWeight(const kVec4 &weight);

        /**
         * @brief Overwrites the bone-weight tuple for a specific vertex.
         * @param vertexIndex Zero-based vertex index.
         * @param weight      New blend-weight tuple.
         */
        void setWeight(size_t vertexIndex, const kVec4 &weight);

        /**
         * @brief Returns the bone-weight tuple for a vertex.
         * @param vertexIndex Zero-based vertex index.
         * @return Four blend weights.
         */
        kVec4 getWeight(size_t vertexIndex);

        /**
         * @brief Returns a copy of the bone-weight buffer.
         * @return Vector of kVec4 weights, one per vertex.
         */
        std::vector<kVec4> getWeights();

        /**
         * @brief Replaces the entire bone-weight buffer.
         * @param newWeights New buffer.
         */
        void setWeights(std::vector<kVec4> newWeights);

        /**
         * @brief Returns the number of vertices in this mesh.
         * @return Vertex count.
         */
        int getVertexCount();

        /**
         * @brief Returns the GPU handle for the Vertex Array Object.
         * @return VAO handle (0 if not yet uploaded).
         */
        uint32_t getVertexArrayObject();

        /**
         * @brief Returns the GPU handle for the position VBO.
         * @return VBO handle (0 if not yet uploaded).
         */
        uint32_t getVertexBuffer();

        /**
         * @brief Returns the GPU handle for the vertex-colour VBO.
         * @return VBO handle (0 if not yet uploaded).
         */
        uint32_t getVertexColorBuffer();

        /**
         * @brief Stores a precomputed normal matrix.
         * @param newNormalMatrix Inverse-transpose of the model matrix (upper 3x3).
         */
        void setNormalMatrix(kMat4 newNormalMatrix);

        /**
         * @brief Returns the stored normal matrix.
         * @return 4x4 matrix whose upper-left 3x3 is the normal matrix.
         */
        kMat4 getNormalMatrix();

        /**
         * @brief Computes the local-space AABB from the vertex position buffer.
         *
         * Called automatically by generateVbo(); safe to call manually if vertices
         * are modified after VBO generation.
         */
        void computeLocalAABB();

        /**
         * @brief Returns the axis-aligned bounding box in local (object) space.
         */
        kAABB getLocalAABB() const;

        /**
         * @brief Returns the AABB transformed into world space.
         *
         * Transforms all 8 corners of the local AABB by the current world matrix
         * and returns the enclosing axis-aligned box.  Call calculateModelMatrix()
         * first to ensure the world transform is up to date.
         */
        kAABB getWorldAABB();

        /**
         * @brief Uploads all vertex attribute data to the GPU.
         *
         * Creates a VAO, one VBO per attribute, and an EBO, then describes
         * the attribute layout to the driver.  Safe to call multiple times;
         * subsequent calls re-upload the data.
         */
        void generateVbo();

        /**
         * @brief Computes per-vertex tangents and bitangents from positions, UVs, and indices.
         * Must be called after all vertices, UVs, normals, and indices have been added and
         * before generateVbo().
         */
        void generateTangents();

        /**
         * @brief Recomputes the normal matrix from the current world transform.
         */
        void calculateNormalMatrix();

        /**
         * @brief Issues a draw call for this mesh via the current kDriver.
         */
        void draw();

        /**
         * @brief Serialises the mesh to JSON.
         * @return JSON object with geometry, transform, and material references.
         */
        json serialize();

        /**
         * @brief Restores the mesh from a JSON object.
         * @param data JSON produced by serialize().
         */
        void deserialize(json data);

        /**
         * @brief Controls the mesh's render visibility.
         * @param newVisible false to skip this mesh during rendering.
         */
        void setVisible(bool newVisible);

        /**
         * @brief Returns whether the mesh is visible.
         * @return true if the mesh will be rendered.
         */
        bool getVisible();

        /**
         * @brief Controls whether the mesh casts shadows.
         * @param newCastShadow true to include in the shadow depth pass.
         */
        void setCastShadow(bool newCastShadow);

        /**
         * @brief Returns whether the mesh casts shadows.
         * @return true if included in the shadow pass.
         */
        bool getCastShadow();

        /**
         * @brief Controls whether the mesh receives shadows.
         * @param newReceiveShadow true to apply shadow on this mesh.
         */
        void setReceiveShadow(bool newReceiveShadow);

        /**
         * @brief Returns whether the mesh receives shadows.
         * @return true if shadow is applied on this mesh.
         */
        bool getReceiveShadow();

        /**
         * @brief Assigns a single bone influence to a vertex (used during loading).
         * @param vertexID Zero-based vertex index.
         * @param boneID   Index into the bone palette.
         * @param weight   Blend weight for this bone.
         */
        void setVertexBoneData(size_t vertexID, int boneID, float weight);

        /**
         * @brief Attaches a skeletal animator to this mesh.
         * @param newAnimator Animator driving the bone transforms.
         */
        void setAnimator(kAnimator *newAnimator);

        /**
         * @brief Returns the attached animator.
         * @return Pointer to the animator, or nullptr if none.
         */
        kAnimator *getAnimator();

        /**
         * @brief Sets whether this mesh is driven by skeletal animation.
         * @param newSkinned true to enable skinning.
         */
        void setSkinned(bool newSkinned);

        /**
         * @brief Returns whether skeletal skinning is enabled.
         * @return true if the mesh is skinned.
         */
        bool getSkinned();

    protected:
    private:
        bool loaded = false;

        kString fileName;
        kString refName;
        kString primitiveType; ///< Empty unless created by kMeshGenerator.

        std::vector<kVec3>     vertices;
        std::vector<uint32_t> indices;
        std::vector<kVec2>     uvs;
        std::vector<kVec3>     vertexColors;
        std::vector<kVec3>     normals;
        std::vector<kVec3>     tangents;
        std::vector<kVec3>     bitangents;
        std::vector<kIvec4>    boneIDs;
        std::vector<kVec4>     weights;

        uint32_t vao         = 0; ///< Vertex Array Object handle.
        uint32_t indicesEbo  = 0; ///< Element Buffer Object handle.

        uint32_t vertexBuffer      = 0; ///< Position VBO.
        uint32_t vertexColorBuffer = 0; ///< Vertex-colour VBO.
        uint32_t uvBuffer          = 0; ///< UV coordinate VBO.
        uint32_t normalBuffer      = 0; ///< Normal VBO.
        uint32_t tangentBuffer     = 0; ///< Tangent VBO.
        uint32_t bitangentBuffer   = 0; ///< Bitangent VBO.
        uint32_t boneIDBuffer      = 0; ///< Bone-index VBO.
        uint32_t weightBuffer      = 0; ///< Bone-weight VBO.

        kMat3 normalMatrix; ///< Inverse-transpose of the model matrix (upper 3x3).

        kAABB localAABB; ///< Bounding box in object (local) space.

        bool isVisible       = true;  ///< Render visibility flag.
        bool isCastShadow    = true;  ///< Shadow-cast flag.
        bool isReceiveShadow = true;  ///< Shadow-receive flag.

        std::map<kString, kBoneInfo> boneInfoMap; ///< Bone name → info lookup.
        int boneCount = 0;                       ///< Total number of bones.

        kAnimator *animator = nullptr; ///< Optional skeletal animator.
        bool isSkinned = false;        ///< Skeletal skinning enabled flag.
    };
}

#endif // KMESH_H
