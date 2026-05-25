#include "knavmesh.h"

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <DetourCommon.h>

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <cstring>
#include <cmath>
#include <algorithm>

namespace kemena
{

// ============================================================================
// RawCompressor — no-op dtTileCacheCompressor (avoids FastLZ dependency)
// ============================================================================

struct RawCompressor : public dtTileCacheCompressor
{
    int maxCompressedSize(const int bufferSize) override { return bufferSize; }

    dtStatus compress(const unsigned char* buffer, const int bufferSize,
                      unsigned char* compressed, const int /*maxCompressedSize*/,
                      int* compressedSize) override
    {
        std::memcpy(compressed, buffer, bufferSize);
        *compressedSize = bufferSize;
        return DT_SUCCESS;
    }

    dtStatus decompress(const unsigned char* compressed, const int compressedSize,
                        unsigned char* buffer, const int /*maxBufferSize*/,
                        int* bufferSize) override
    {
        std::memcpy(buffer, compressed, compressedSize);
        *bufferSize = compressedSize;
        return DT_SUCCESS;
    }
};

// ============================================================================
// MeshProcess — injects off-mesh connections during tile building
// ============================================================================

struct MeshProcess : public dtTileCacheMeshProcess
{
    const std::vector<kNavLink> *links = nullptr;

    void process(struct dtNavMeshCreateParams *params,
                 unsigned char * /*polyAreas*/,
                 unsigned short * /*polyFlags*/) override
    {
        if (!links || links->empty())
            return;

        // Collect links that intersect this tile's AABB
        const float *bmin = params->bmin;
        const float *bmax = params->bmax;

        std::vector<float>        offMeshVerts;
        std::vector<float>        offMeshRads;
        std::vector<unsigned char> offMeshDirs;
        std::vector<unsigned char> offMeshAreas;
        std::vector<unsigned short> offMeshFlags;
        std::vector<unsigned int>  offMeshIds;

        for (const auto &lnk : *links)
        {
            // Include if either endpoint is within the tile's XZ bounds (with margin)
            const float margin = lnk.radius;
            bool startIn = (lnk.start.x >= bmin[0] - margin && lnk.start.x <= bmax[0] + margin &&
                            lnk.start.z >= bmin[2] - margin && lnk.start.z <= bmax[2] + margin);
            bool endIn   = (lnk.end.x   >= bmin[0] - margin && lnk.end.x   <= bmax[0] + margin &&
                            lnk.end.z   >= bmin[2] - margin && lnk.end.z   <= bmax[2] + margin);

            if (!startIn && !endIn)
                continue;

            offMeshVerts.push_back(lnk.start.x);
            offMeshVerts.push_back(lnk.start.y);
            offMeshVerts.push_back(lnk.start.z);
            offMeshVerts.push_back(lnk.end.x);
            offMeshVerts.push_back(lnk.end.y);
            offMeshVerts.push_back(lnk.end.z);

            offMeshRads.push_back(lnk.radius);
            offMeshDirs.push_back(lnk.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
            offMeshAreas.push_back(RC_WALKABLE_AREA);
            offMeshFlags.push_back(0xFFFF);
            offMeshIds.push_back(1000 + static_cast<unsigned int>(offMeshIds.size()));
        }

        if (offMeshVerts.empty())
            return;

        params->offMeshConVerts  = offMeshVerts.data();
        params->offMeshConRad    = offMeshRads.data();
        params->offMeshConDir    = offMeshDirs.data();
        params->offMeshConAreas  = offMeshAreas.data();
        params->offMeshConFlags  = offMeshFlags.data();
        params->offMeshConUserID = offMeshIds.data();
        params->offMeshConCount  = static_cast<int>(offMeshIds.size());
    }
};

// ============================================================================
// LinearAllocator — simple bump-pointer used by dtTileCache
// ============================================================================

struct LinearAllocator : public dtTileCacheAlloc
{
    unsigned char *buffer = nullptr;
    size_t capacity       = 0;
    size_t top            = 0;
    size_t high           = 0;

    explicit LinearAllocator(const size_t cap)
        : capacity(cap)
    {
        buffer = new unsigned char[cap];
    }

    ~LinearAllocator() override { delete[] buffer; }

    void reset() override { high = std::max(high, top); top = 0; }
    void* alloc(const size_t size) override
    {
        if (!buffer || top + size > capacity) return nullptr;
        unsigned char *mem = &buffer[top];
        top += size;
        return mem;
    }
    void free(void* /*ptr*/) override {}
};

// ============================================================================
// Impl
// ============================================================================

struct kNavMesh::Impl
{
    dtNavMesh      *navMesh      = nullptr;
    dtNavMeshQuery *navQuery     = nullptr;
    dtTileCache    *tileCache    = nullptr;

    RawCompressor   compressor;
    MeshProcess     meshProcess;
    LinearAllocator allocator   { 32 * 1024 };

    bool baked = false;

    ~Impl() { reset(); }

    void reset()
    {
        if (navQuery)  { dtFreeNavMeshQuery(navQuery); navQuery = nullptr; }
        if (tileCache) { dtFreeTileCache(tileCache);   tileCache = nullptr; }
        if (navMesh)   { dtFreeNavMesh(navMesh);       navMesh = nullptr; }
        baked = false;
    }
};

// ============================================================================
// Helpers
// ============================================================================

static bool buildTiledMesh(kNavMesh::Impl *impl,
                            const std::vector<float> &verts,
                            const std::vector<int>   &tris,
                            const kNavBuildConfig    &cfg,
                            const std::vector<kNavLink> &links)
{
    const int nverts = static_cast<int>(verts.size()) / 3;
    const int ntris  = static_cast<int>(tris.size()) / 3;

    float bmin[3], bmax[3];
    rcCalcBounds(verts.data(), nverts, bmin, bmax);

    const int tw = static_cast<int>(std::ceil((bmax[0] - bmin[0]) / (cfg.tileSize * cfg.cellSize)));
    const int th = static_cast<int>(std::ceil((bmax[2] - bmin[2]) / (cfg.tileSize * cfg.cellSize)));

    // Setup dtNavMesh
    dtNavMeshParams nmParams{};
    dtVcopy(nmParams.orig, bmin);
    nmParams.tileWidth  = cfg.tileSize * cfg.cellSize;
    nmParams.tileHeight = cfg.tileSize * cfg.cellSize;
    nmParams.maxTiles   = tw * th * 4;
    nmParams.maxPolys   = 16384;

    impl->navMesh = dtAllocNavMesh();
    if (!impl->navMesh) return false;
    if (dtStatusFailed(impl->navMesh->init(&nmParams))) return false;

    // Setup dtTileCache
    dtTileCacheParams tcParams{};
    dtVcopy(tcParams.orig, bmin);
    tcParams.cs         = cfg.cellSize;
    tcParams.ch         = cfg.cellHeight;
    tcParams.width      = static_cast<int>(cfg.tileSize);
    tcParams.height     = static_cast<int>(cfg.tileSize);
    tcParams.walkableHeight = cfg.agentHeight;
    tcParams.walkableRadius = cfg.agentRadius;
    tcParams.walkableClimb  = cfg.agentMaxClimb;
    tcParams.maxSimplificationError = cfg.edgeMaxError;
    tcParams.maxTiles   = tw * th * 4;
    tcParams.maxObstacles = 128;

    impl->tileCache = dtAllocTileCache();
    if (!impl->tileCache) return false;
    if (dtStatusFailed(impl->tileCache->init(&tcParams,
                                              &impl->allocator,
                                              &impl->compressor,
                                              &impl->meshProcess)))
        return false;

    impl->meshProcess.links = &links;

    // Rasterise each tile
    for (int ty = 0; ty < th; ++ty)
    {
        for (int tx = 0; tx < tw; ++tx)
        {
            float tbmin[3], tbmax[3];
            tbmin[0] = bmin[0] + tx * cfg.tileSize * cfg.cellSize;
            tbmin[1] = bmin[1];
            tbmin[2] = bmin[2] + ty * cfg.tileSize * cfg.cellSize;
            tbmax[0] = tbmin[0] + cfg.tileSize * cfg.cellSize;
            tbmax[1] = bmax[1];
            tbmax[2] = tbmin[2] + cfg.tileSize * cfg.cellSize;

            rcConfig rcCfg{};
            rcCfg.cs               = cfg.cellSize;
            rcCfg.ch               = cfg.cellHeight;
            rcCfg.walkableHeight   = static_cast<int>(std::ceil(cfg.agentHeight   / cfg.cellHeight));
            rcCfg.walkableRadius   = static_cast<int>(std::ceil(cfg.agentRadius   / cfg.cellSize));
            rcCfg.walkableClimb    = static_cast<int>(std::floor(cfg.agentMaxClimb / cfg.cellHeight));
            rcCfg.walkableSlopeAngle = cfg.agentMaxSlope;
            rcCfg.minRegionArea    = static_cast<int>(cfg.regionMinSize  * cfg.regionMinSize);
            rcCfg.mergeRegionArea  = static_cast<int>(cfg.regionMergeSize * cfg.regionMergeSize);
            rcCfg.maxEdgeLen       = static_cast<int>(cfg.edgeMaxLen / cfg.cellSize);
            rcCfg.maxSimplificationError = cfg.edgeMaxError;
            rcCfg.maxVertsPerPoly  = cfg.maxVertsPerPoly;
            rcCfg.detailSampleDist = cfg.detailSampleDist < 0.9f ? 0.0f : cfg.cellSize * cfg.detailSampleDist;
            rcCfg.detailSampleMaxError = cfg.cellHeight * cfg.detailSampleMaxError;
            rcCfg.tileSize         = static_cast<int>(cfg.tileSize);
            rcCfg.borderSize       = rcCfg.walkableRadius + 3;
            rcCfg.width            = rcCfg.tileSize + rcCfg.borderSize * 2;
            rcCfg.height           = rcCfg.tileSize + rcCfg.borderSize * 2;

            // Expand tile bounds by border
            tbmin[0] -= rcCfg.borderSize * cfg.cellSize;
            tbmin[2] -= rcCfg.borderSize * cfg.cellSize;
            tbmax[0] += rcCfg.borderSize * cfg.cellSize;
            tbmax[2] += rcCfg.borderSize * cfg.cellSize;

            rcVcopy(rcCfg.bmin, tbmin);
            rcVcopy(rcCfg.bmax, tbmax);

            rcContext ctx;

            // Heightfield
            rcHeightfield *solid = rcAllocHeightfield();
            if (!solid) continue;
            if (!rcCreateHeightfield(&ctx, *solid, rcCfg.width, rcCfg.height,
                                     rcCfg.bmin, rcCfg.bmax, rcCfg.cs, rcCfg.ch))
            { rcFreeHeightField(solid); continue; }

            std::vector<unsigned char> triAreas(ntris, 0);
            rcMarkWalkableTriangles(&ctx, rcCfg.walkableSlopeAngle,
                                    verts.data(), nverts,
                                    tris.data(), ntris, triAreas.data());
            rcRasterizeTriangles(&ctx, verts.data(), nverts,
                                 tris.data(), triAreas.data(), ntris,
                                 *solid, rcCfg.walkableClimb);

            rcFilterLowHangingWalkableObstacles(&ctx, rcCfg.walkableClimb, *solid);
            rcFilterLedgeSpans(&ctx, rcCfg.walkableHeight, rcCfg.walkableClimb, *solid);
            rcFilterWalkableLowHeightSpans(&ctx, rcCfg.walkableHeight, *solid);

            // Compact heightfield
            rcCompactHeightfield *chf = rcAllocCompactHeightfield();
            if (!chf) { rcFreeHeightField(solid); continue; }
            rcBuildCompactHeightfield(&ctx, rcCfg.walkableHeight, rcCfg.walkableClimb, *solid, *chf);
            rcFreeHeightField(solid);
            rcErodeWalkableArea(&ctx, rcCfg.walkableRadius, *chf);

            // Height layers → TileCache tiles
            rcHeightfieldLayerSet *lset = rcAllocHeightfieldLayerSet();
            if (!lset) { rcFreeCompactHeightfield(chf); continue; }
            if (!rcBuildHeightfieldLayers(&ctx, *chf, rcCfg.borderSize,
                                          rcCfg.walkableHeight, *lset))
            { rcFreeCompactHeightfield(chf); rcFreeHeightfieldLayerSet(lset); continue; }
            rcFreeCompactHeightfield(chf);

            for (int i = 0; i < lset->nlayers; ++i)
            {
                dtTileCacheLayerHeader header{};
                header.magic   = DT_TILECACHE_MAGIC;
                header.version = DT_TILECACHE_VERSION;
                header.tx = tx;
                header.ty = ty;
                header.tlayer = i;
                dtVcopy(header.bmin, lset->layers[i].bmin);
                dtVcopy(header.bmax, lset->layers[i].bmax);
                header.width  = static_cast<unsigned char>(lset->layers[i].width);
                header.height = static_cast<unsigned char>(lset->layers[i].height);
                header.minx   = static_cast<unsigned char>(lset->layers[i].minx);
                header.maxx   = static_cast<unsigned char>(lset->layers[i].maxx);
                header.miny   = static_cast<unsigned char>(lset->layers[i].miny);
                header.maxy   = static_cast<unsigned char>(lset->layers[i].maxy);
                header.hmin   = static_cast<unsigned short>(lset->layers[i].hmin);
                header.hmax   = static_cast<unsigned short>(lset->layers[i].hmax);

                unsigned char *tdata = nullptr;
                int            tsize = 0;
                dtStatus st = dtBuildTileCacheLayer(&impl->compressor, &header,
                                                    lset->layers[i].heights,
                                                    lset->layers[i].areas,
                                                    lset->layers[i].cons,
                                                    &tdata, &tsize);
                if (dtStatusFailed(st)) continue;

                dtCompressedTileRef ref;
                impl->tileCache->addTile(tdata, tsize, DT_COMPRESSEDTILE_FREE_DATA, &ref);
            }
            rcFreeHeightfieldLayerSet(lset);
        }
    }

    // Build initial nav mesh tiles
    for (int ty = 0; ty < th; ++ty)
        for (int tx = 0; tx < tw; ++tx)
            impl->tileCache->buildNavMeshTilesAt(tx, ty, impl->navMesh);

    return true;
}

static bool buildSingleMesh(kNavMesh::Impl *impl,
                             const std::vector<float> &verts,
                             const std::vector<int>   &tris,
                             const kNavBuildConfig    &cfg,
                             const std::vector<kNavLink> &links)
{
    const int nverts = static_cast<int>(verts.size()) / 3;
    const int ntris  = static_cast<int>(tris.size()) / 3;

    float bmin[3], bmax[3];
    rcCalcBounds(verts.data(), nverts, bmin, bmax);

    rcConfig rcCfg{};
    rcCfg.cs             = cfg.cellSize;
    rcCfg.ch             = cfg.cellHeight;
    rcCfg.walkableHeight = static_cast<int>(std::ceil(cfg.agentHeight   / cfg.cellHeight));
    rcCfg.walkableRadius = static_cast<int>(std::ceil(cfg.agentRadius   / cfg.cellSize));
    rcCfg.walkableClimb  = static_cast<int>(std::floor(cfg.agentMaxClimb / cfg.cellHeight));
    rcCfg.walkableSlopeAngle = cfg.agentMaxSlope;
    rcCfg.minRegionArea  = static_cast<int>(cfg.regionMinSize  * cfg.regionMinSize);
    rcCfg.mergeRegionArea = static_cast<int>(cfg.regionMergeSize * cfg.regionMergeSize);
    rcCfg.maxEdgeLen     = static_cast<int>(cfg.edgeMaxLen / cfg.cellSize);
    rcCfg.maxSimplificationError = cfg.edgeMaxError;
    rcCfg.maxVertsPerPoly = cfg.maxVertsPerPoly;
    rcCfg.detailSampleDist = cfg.detailSampleDist < 0.9f ? 0.0f : cfg.cellSize * cfg.detailSampleDist;
    rcCfg.detailSampleMaxError = cfg.cellHeight * cfg.detailSampleMaxError;
    rcCalcGridSize(bmin, bmax, cfg.cellSize, &rcCfg.width, &rcCfg.height);
    rcVcopy(rcCfg.bmin, bmin);
    rcVcopy(rcCfg.bmax, bmax);

    rcContext ctx;

    rcHeightfield *solid = rcAllocHeightfield();
    if (!solid) return false;
    if (!rcCreateHeightfield(&ctx, *solid, rcCfg.width, rcCfg.height,
                             rcCfg.bmin, rcCfg.bmax, rcCfg.cs, rcCfg.ch))
    { rcFreeHeightField(solid); return false; }

    std::vector<unsigned char> triAreas(ntris, 0);
    rcMarkWalkableTriangles(&ctx, rcCfg.walkableSlopeAngle,
                            verts.data(), nverts, tris.data(), ntris, triAreas.data());
    rcRasterizeTriangles(&ctx, verts.data(), nverts,
                         tris.data(), triAreas.data(), ntris, *solid, rcCfg.walkableClimb);

    rcFilterLowHangingWalkableObstacles(&ctx, rcCfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, rcCfg.walkableHeight, rcCfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, rcCfg.walkableHeight, *solid);

    rcCompactHeightfield *chf = rcAllocCompactHeightfield();
    if (!chf) { rcFreeHeightField(solid); return false; }
    rcBuildCompactHeightfield(&ctx, rcCfg.walkableHeight, rcCfg.walkableClimb, *solid, *chf);
    rcFreeHeightField(solid);
    rcErodeWalkableArea(&ctx, rcCfg.walkableRadius, *chf);

    // Region building
    if (cfg.partition == kNavPartitionType::Watershed)
    {
        rcBuildDistanceField(&ctx, *chf);
        rcBuildRegions(&ctx, *chf, 0, rcCfg.minRegionArea, rcCfg.mergeRegionArea);
    }
    else if (cfg.partition == kNavPartitionType::Monotone)
    {
        rcBuildRegionsMonotone(&ctx, *chf, 0, rcCfg.minRegionArea, rcCfg.mergeRegionArea);
    }
    else
    {
        rcBuildLayerRegions(&ctx, *chf, 0, rcCfg.minRegionArea);
    }

    rcContourSet *cset = rcAllocContourSet();
    if (!cset) { rcFreeCompactHeightfield(chf); return false; }
    rcBuildContours(&ctx, *chf, rcCfg.maxSimplificationError, rcCfg.maxEdgeLen, *cset);

    rcPolyMesh *pmesh = rcAllocPolyMesh();
    if (!pmesh) { rcFreeCompactHeightfield(chf); rcFreeContourSet(cset); return false; }
    rcBuildPolyMesh(&ctx, *cset, rcCfg.maxVertsPerPoly, *pmesh);

    rcPolyMeshDetail *dmesh = rcAllocPolyMeshDetail();
    if (!dmesh)
    { rcFreeCompactHeightfield(chf); rcFreeContourSet(cset); rcFreePolyMesh(pmesh); return false; }
    rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, rcCfg.detailSampleDist, rcCfg.detailSampleMaxError, *dmesh);
    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);

    // Mark all polys walkable
    for (int i = 0; i < pmesh->npolys; ++i)
        if (pmesh->areas[i] == RC_WALKABLE_AREA)
            pmesh->flags[i] = 0xFFFF;

    // Off-mesh connections
    std::vector<float>         omVerts;
    std::vector<float>         omRads;
    std::vector<unsigned char> omDirs;
    std::vector<unsigned char> omAreas;
    std::vector<unsigned short> omFlags;
    std::vector<unsigned int>  omIds;
    for (const auto &lnk : links)
    {
        omVerts.push_back(lnk.start.x); omVerts.push_back(lnk.start.y); omVerts.push_back(lnk.start.z);
        omVerts.push_back(lnk.end.x);   omVerts.push_back(lnk.end.y);   omVerts.push_back(lnk.end.z);
        omRads.push_back(lnk.radius);
        omDirs.push_back(lnk.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
        omAreas.push_back(RC_WALKABLE_AREA);
        omFlags.push_back(0xFFFF);
        omIds.push_back(1000 + static_cast<unsigned int>(omIds.size()));
    }

    dtNavMeshCreateParams params{};
    std::memset(&params, 0, sizeof(params));
    params.verts            = pmesh->verts;
    params.vertCount        = pmesh->nverts;
    params.polys            = pmesh->polys;
    params.polyAreas        = pmesh->areas;
    params.polyFlags        = pmesh->flags;
    params.polyCount        = pmesh->npolys;
    params.nvp              = pmesh->nvp;
    params.detailMeshes     = dmesh->meshes;
    params.detailVerts      = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris       = dmesh->tris;
    params.detailTriCount   = dmesh->ntris;
    params.walkableHeight   = cfg.agentHeight;
    params.walkableRadius   = cfg.agentRadius;
    params.walkableClimb    = cfg.agentMaxClimb;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = rcCfg.cs;
    params.ch = rcCfg.ch;
    params.buildBvTree = true;

    if (!omIds.empty())
    {
        params.offMeshConVerts  = omVerts.data();
        params.offMeshConRad    = omRads.data();
        params.offMeshConDir    = omDirs.data();
        params.offMeshConAreas  = omAreas.data();
        params.offMeshConFlags  = omFlags.data();
        params.offMeshConUserID = omIds.data();
        params.offMeshConCount  = static_cast<int>(omIds.size());
    }

    unsigned char *navData  = nullptr;
    int            navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
    {
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }
    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    impl->navMesh = dtAllocNavMesh();
    if (!impl->navMesh) { dtFree(navData); return false; }
    if (dtStatusFailed(impl->navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA)))
        return false;

    return true;
}

// ============================================================================
// kNavMesh
// ============================================================================

kNavMesh::kNavMesh()  : m_impl(new Impl) {}
kNavMesh::~kNavMesh() { delete m_impl; }

bool kNavMesh::bake(const std::vector<float>   &verts,
                    const std::vector<int>      &tris,
                    const kNavBuildConfig       &config,
                    const std::vector<kNavLink> &links)
{
    if (verts.empty() || tris.empty()) return false;

    m_impl->reset();

    bool ok = (config.tileSize > 0.0f)
        ? buildTiledMesh(m_impl, verts, tris, config, links)
        : buildSingleMesh(m_impl, verts, tris, config, links);

    if (!ok) { m_impl->reset(); return false; }

    // Build the nav mesh query
    m_impl->navQuery = dtAllocNavMeshQuery();
    if (!m_impl->navQuery || dtStatusFailed(m_impl->navQuery->init(m_impl->navMesh, 2048)))
    {
        m_impl->reset();
        return false;
    }

    m_impl->baked = true;
    return true;
}

void kNavMesh::clear()   { m_impl->reset(); }
bool kNavMesh::isBaked() const { return m_impl->baked; }

std::vector<kVec3> kNavMesh::findPath(const kVec3 &start,
                                       const kVec3 &end,
                                       int maxPoints) const
{
    if (!m_impl->baked) return {};

    const float extents[3] = { 2.0f, 4.0f, 2.0f };
    dtQueryFilter filter;

    float sPos[3] = { start.x, start.y, start.z };
    float ePos[3] = { end.x,   end.y,   end.z   };

    dtPolyRef startRef = 0, endRef = 0;
    float nearestStart[3], nearestEnd[3];
    m_impl->navQuery->findNearestPoly(sPos, extents, &filter, &startRef, nearestStart);
    m_impl->navQuery->findNearestPoly(ePos, extents, &filter, &endRef,   nearestEnd);

    if (!startRef || !endRef) return {};

    static const int MAX_POLY = 512;
    dtPolyRef polys[MAX_POLY];
    int       npolys = 0;
    m_impl->navQuery->findPath(startRef, endRef, nearestStart, nearestEnd,
                                &filter, polys, &npolys, MAX_POLY);
    if (npolys == 0) return {};

    float smoothEnd[3];
    dtVcopy(smoothEnd, nearestEnd);
    if (polys[npolys - 1] != endRef)
        m_impl->navQuery->closestPointOnPoly(polys[npolys - 1], nearestEnd, smoothEnd, nullptr);

    std::vector<float> path(maxPoints * 3);
    int npath = 0;
    m_impl->navQuery->findStraightPath(nearestStart, smoothEnd,
                                        polys, npolys,
                                        path.data(), nullptr, nullptr,
                                        &npath, maxPoints, 0);

    std::vector<kVec3> result;
    result.reserve(npath);
    for (int i = 0; i < npath; ++i)
        result.push_back({ path[i * 3], path[i * 3 + 1], path[i * 3 + 2] });

    return result;
}

kVec3 kNavMesh::findNearestPoint(const kVec3 &pos, const kVec3 &extents) const
{
    if (!m_impl->baked) return pos;

    float p[3]   = { pos.x, pos.y, pos.z };
    float ext[3] = { extents.x, extents.y, extents.z };
    dtQueryFilter filter;
    dtPolyRef ref = 0;
    float nearest[3];
    dtStatus st = m_impl->navQuery->findNearestPoly(p, ext, &filter, &ref, nearest);
    if (dtStatusFailed(st) || !ref) return pos;
    return { nearest[0], nearest[1], nearest[2] };
}

bool kNavMesh::isPointOnMesh(const kVec3 &pos, const kVec3 &extents) const
{
    if (!m_impl->baked) return false;
    float p[3]   = { pos.x, pos.y, pos.z };
    float ext[3] = { extents.x, extents.y, extents.z };
    dtQueryFilter filter;
    dtPolyRef ref = 0;
    float nearest[3];
    m_impl->navQuery->findNearestPoly(p, ext, &filter, &ref, nearest);
    return ref != 0;
}

void kNavMesh::getDebugLines(std::vector<kVec3> &out) const
{
    out.clear();
    if (!m_impl->baked || !m_impl->navMesh)
        return;

    const dtNavMesh *mesh = m_impl->navMesh;

    // Walk every tile → poly → detail triangle, emitting the three edges of
    // each triangle as line segments (standard Detour navmesh extraction).
    for (int i = 0; i < mesh->getMaxTiles(); ++i)
    {
        const dtMeshTile *tile = mesh->getTile(i);
        if (!tile || !tile->header)
            continue;

        for (int p = 0; p < tile->header->polyCount; ++p)
        {
            const dtPoly *poly = &tile->polys[p];
            if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                continue;

            const dtPolyDetail *pd = &tile->detailMeshes[p];

            for (int t = 0; t < pd->triCount; ++t)
            {
                const unsigned char *tri = &tile->detailTris[(pd->triBase + t) * 4];

                kVec3 v[3];
                for (int k = 0; k < 3; ++k)
                {
                    const float *vp;
                    if (tri[k] < poly->vertCount)
                        vp = &tile->verts[poly->verts[tri[k]] * 3];
                    else
                        vp = &tile->detailVerts[(pd->vertBase + tri[k] - poly->vertCount) * 3];
                    v[k] = kVec3(vp[0], vp[1], vp[2]);
                }

                out.push_back(v[0]); out.push_back(v[1]);
                out.push_back(v[1]); out.push_back(v[2]);
                out.push_back(v[2]); out.push_back(v[0]);
            }
        }
    }
}

void *kNavMesh::getNavMesh()      const { return m_impl->navMesh; }
void *kNavMesh::getNavMeshQuery() const { return m_impl->navQuery; }
void *kNavMesh::getTileCache()    const { return m_impl->tileCache; }

} // namespace kemena
