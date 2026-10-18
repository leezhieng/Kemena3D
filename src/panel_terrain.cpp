/**
 * @file panel_terrain.cpp
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#include "panel_terrain.h"
#include "util.h"
#include "mainmenu.h"   // for terrainDockId / terrainDockRestore (panel docking persistence)
#include "commands.h"   // for TerrainEditCommand
#include "panel_project.h"   // for thumbnail icons in the layer texture picker

#include <imgui.h>
#include <set>
#include <random>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cctype>

PanelTerrain::PanelTerrain(kGuiManager *setGuiManager, Manager *setManager)
    : gui(setGuiManager), manager(setManager)
{
}

PanelTerrain::EditSnapshot *PanelTerrain::findOrCreateEditSnapshot(kTerrain *tile)
{
    for (auto &s : m_editSnapshots)
        if (s.tile == tile)
            return &s;

    EditSnapshot s;
    s.tile = tile;
    m_editSnapshots.push_back(std::move(s));
    return &m_editSnapshots.back();
}

void PanelTerrain::beginTerrainEdit(kTerrain *tile, bool height, bool weight)
{
    if (!tile)
        return;

    EditSnapshot *s = findOrCreateEditSnapshot(tile);

    if (height && !s->height)
    {
        // Snapshot the *base* field: sculpting and height painting only ever
        // modify the base, and the generated modifier layers are recomposited
        // on top afterwards.
        const float *data = tile->getBaseHeightData();
        const size_t n = tile->getHeightDataSize();
        if (data && n > 0)
        {
            s->height = true;
            s->heightBefore.assign(data, data + n);
        }
    }
    if (weight && !s->weight)
    {
        const unsigned char *data = tile->getWeightData();
        const size_t n = tile->getWeightDataSize();
        if (data && n > 0)
        {
            s->weight = true;
            s->weightBefore.assign(data, data + n);
        }
    }
}

void PanelTerrain::commitTerrainEdit()
{
    if (m_editSnapshots.empty())
        return;

    // One command holds every tile touched during the stroke, so a stroke that
    // crosses a tile border still undoes in a single step.
    auto cmd = std::make_unique<TerrainEditCommand>();
    cmd->manager = manager;

    for (const auto &s : m_editSnapshots)
    {
        kTerrain *tile = s.tile;
        if (!tile)
            continue;

        const int res = tile->getHeightRes();
        TerrainEditCommand::TileDiff diff;
        diff.tile = tile;
        diff.res = res;

        // --- Height field diff (base field only) ---
        if (s.height && s.heightBefore.size() == static_cast<size_t>(res) * res)
        {
            const float *after = tile->getBaseHeightData();
            if (after)
            {
                int minX = res, minZ = res, maxX = -1, maxZ = -1;
                for (int z = 0; z < res; ++z)
                    for (int x = 0; x < res; ++x)
                    {
                        size_t i = static_cast<size_t>(z) * res + x;
                        if (after[i] != s.heightBefore[i])
                        {
                            if (x < minX) minX = x;
                            if (x > maxX) maxX = x;
                            if (z < minZ) minZ = z;
                            if (z > maxZ) maxZ = z;
                        }
                    }

                if (maxX >= minX && maxZ >= minZ)
                {
                    int w = maxX - minX + 1;
                    int h = maxZ - minZ + 1;
                    diff.hasHeight = true;
                    diff.hx0 = minX; diff.hz0 = minZ; diff.hxc = w; diff.hzc = h;
                    diff.heightBefore.resize(static_cast<size_t>(w) * h);
                    diff.heightAfter.resize(static_cast<size_t>(w) * h);
                    for (int z = 0; z < h; ++z)
                    {
                        size_t dst = static_cast<size_t>(z) * w;
                        size_t src = static_cast<size_t>(minZ + z) * res + minX;
                        std::copy(s.heightBefore.begin() + src,
                                  s.heightBefore.begin() + src + w,
                                  diff.heightBefore.begin() + dst);
                        std::copy(after + src, after + src + w,
                                  diff.heightAfter.begin() + dst);
                    }
                }
            }
        }

        // --- Weight (splat) map diff ---
        if (s.weight &&
            s.weightBefore.size() == static_cast<size_t>(res) * res * kTerrain::kMaxTerrainLayers)
        {
            const unsigned char *after = tile->getWeightData();
            if (after)
            {
                const int L = kTerrain::kMaxTerrainLayers;
                int minX = res, minZ = res, maxX = -1, maxZ = -1;
                for (int z = 0; z < res; ++z)
                    for (int x = 0; x < res; ++x)
                    {
                        size_t base = (static_cast<size_t>(z) * res + x) * L;
                        bool differs = false;
                        for (int c = 0; c < L; ++c)
                            if (after[base + c] != s.weightBefore[base + c])
                            {
                                differs = true;
                                break;
                            }
                        if (differs)
                        {
                            if (x < minX) minX = x;
                            if (x > maxX) maxX = x;
                            if (z < minZ) minZ = z;
                            if (z > maxZ) maxZ = z;
                        }
                    }

                if (maxX >= minX && maxZ >= minZ)
                {
                    int w = maxX - minX + 1;
                    int h = maxZ - minZ + 1;
                    size_t stride = static_cast<size_t>(w) * L;
                    diff.hasWeight = true;
                    diff.wx0 = minX; diff.wz0 = minZ; diff.wxc = w; diff.wzc = h;
                    diff.weightBefore.resize(static_cast<size_t>(h) * stride);
                    diff.weightAfter.resize(static_cast<size_t>(h) * stride);
                    for (int z = 0; z < h; ++z)
                    {
                        size_t dst = static_cast<size_t>(z) * stride;
                        size_t src = (static_cast<size_t>(minZ + z) * res + minX) * L;
                        std::copy(s.weightBefore.begin() + src,
                                  s.weightBefore.begin() + src + stride,
                                  diff.weightBefore.begin() + dst);
                        std::copy(after + src, after + src + stride,
                                  diff.weightAfter.begin() + dst);
                    }
                }
            }
        }

        if (diff.hasHeight || diff.hasWeight)
            cmd->diffs.push_back(std::move(diff));
    }

    if (!cmd->diffs.empty() && manager)
        manager->undoRedo.push(std::move(cmd));

    m_editSnapshots.clear();
}

void PanelTerrain::cancelTerrainEdit()
{
    m_editSnapshots.clear();
}

void PanelTerrain::sculptAtWorldPos(const kVec3 &worldPos)
{
    if (!manager || !manager->terrainManager)
        return;

    // The tile under the brush centre, used as the Flatten height reference.
    kTerrain *centreTile = nullptr;
    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        int hx, hz;
        if (tile->worldToHeightmap(worldPos, hx, hz))
        {
            centreTile = tile;
            break;
        }
    }

    // On the first stroke in Flatten mode, sample the target height.
    if (sculpt.mode == kTerrain::BrushMode::Flatten && centreTile &&
        sculpt.affectedTile != centreTile)
    {
        sculpt.flattenHeight = centreTile->sampleHeight(worldPos);
    }

    // Apply the stamp to *every* tile the brush circle touches, not just the one
    // under the centre. Each tile derives its falloff from the same world-space
    // distance, so the shared edge rows rise by exactly the same amount and the
    // seam stays closed. (Previously only the centre tile was edited, so the two
    // sides of a border were raised by different amounts and the edge shape was
    // wrong.)
    const float brushRadius = sculpt.radius;
    int applied = 0;

    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        // Brush circle vs tile rectangle, in world XZ.
        const float tileSize = tile->getWorldSize();
        const float minX = static_cast<float>(tile->getGridX()) * tileSize;
        const float minZ = static_cast<float>(tile->getGridZ()) * tileSize;

        if (worldPos.x + brushRadius < minX || worldPos.x - brushRadius > minX + tileSize ||
            worldPos.z + brushRadius < minZ || worldPos.z - brushRadius > minZ + tileSize)
            continue;

        beginTerrainEdit(tile, true, false);
        tile->applyBrush(worldPos, brushRadius, sculpt.strength,
                         sculpt.mode, sculpt.flattenHeight);

        // Fast polygon update for live visual feedback.
        tile->updateMesh();

        sculpt.affectedTile = tile;
        ++applied;
    }

    if (applied == 0)
        return;

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();

    sculpt.needsRebuild = true;
}

void PanelTerrain::rebuildSculptedTile()
{
    if (!sculpt.needsRebuild)
        return;

    // Full rebuild (rather than the cheap updateMesh used during the stroke) so the
    // boundary samples are re-stitched against the neighbours: sculpting close to a
    // tile border otherwise left a visible crack. A stroke can span several tiles,
    // so every tile recorded for this stroke is rebuilt, not just the last one
    // touched. rebuildMesh() preserves the material, rebuildTileMesh() keeps the
    // selection valid.
    bool rebuilt = false;
    for (const auto &snapshot : m_editSnapshots)
        if (snapshot.tile)
        {
            rebuildTileMesh(snapshot.tile);
            rebuilt = true;
        }

    if (!rebuilt && sculpt.affectedTile)
        rebuildTileMesh(sculpt.affectedTile);

    if (manager && manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    sculpt.needsRebuild = false;

    // Mouse-up: record the whole stroke as a single undoable command.
    commitTerrainEdit();
}

void PanelTerrain::paintAtWorldPos(const kVec3 &worldPos, bool lower)
{
    if (!manager || !manager->terrainManager)
        return;

    // Find the terrain tile under worldPos
    kTerrain *foundTile = nullptr;
    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        int hx, hz;
        if (tile->worldToHeightmap(worldPos, hx, hz))
        {
            foundTile = tile;
            break;
        }
    }

    if (!foundTile)
        return;

    beginTerrainEdit(foundTile, true, false);

    // Apply height adjustment: lower = decrease (dark/down), raise = increase (light/up)
    const float radius = sculpt.radius; // reuse sculpt radius for paint size
    const float effectiveStrength = paint.strength * (lower ? -1.0f : 1.0f);

    int cx, cz;
    if (!foundTile->worldToHeightmap(worldPos, cx, cz))
        return;

    float sampleSpacing = foundTile->getWorldSize() / static_cast<float>(foundTile->getHeightRes() - 1);
    int radSamples = static_cast<int>(std::ceil(radius / sampleSpacing));
    int res = foundTile->getHeightRes();
    int xMin = (std::max)(0, cx - radSamples);
    int xMax = (std::min)(res - 1, cx + radSamples);
    int zMin = (std::max)(0, cz - radSamples);
    int zMax = (std::min)(res - 1, cz + radSamples);

    float worldRadiusSq = radius * radius;

    // Height painting is a sculpt of the base field, not of the composite, so it
    // survives recomposeHeight() and is captured by the base-field undo diff.
    float *data = foundTile->getBaseHeightData();
    if (!data)
        return;

    for (int z = zMin; z <= zMax; ++z)
    {
        for (int x = xMin; x <= xMax; ++x)
        {
            kVec3 sampleWorld = foundTile->heightmapToWorld(x, z);
            float dx = sampleWorld.x - worldPos.x;
            float dz = sampleWorld.z - worldPos.z;
            float distSq = dx * dx + dz * dz;

            if (distSq > worldRadiusSq)
                continue;

            float dist = std::sqrt(distSq);
            float t = dist / radius;
            // Cosine falloff, blended with fade control
            float core = std::cos(t * 3.14159265358979323846f * 0.5f);
            float falloff = core * (1.0f - paint.fade) + paint.fade * (1.0f - t);
            falloff = (std::max)(0.0f, falloff);
            // Same reasoning as the sculpt brush: scale by the brush's world radius
            // so a single click changes the height perceptibly.
            falloff *= effectiveStrength * (std::max)(radius * 0.5f, sampleSpacing);

            int idx = z * res + x;
            float oldVal = data[idx];
            data[idx] = oldVal + falloff;
        }
    }

    // Fold the modifier layers over the edited base, then refresh the mesh.
    foundTile->recomposeHeight();

    // Fast polygon update for live visual feedback
    foundTile->updateMesh();

    paint.affectedTile = foundTile;
    paint.needsRebuild = true;
}

void PanelTerrain::rebuildPaintedTile()
{
    if (!paint.needsRebuild || !paint.affectedTile)
        return;

    // Height painting moves geometry too, so rebuild + re-stitch the seams (see
    // rebuildSculptedTile()).
    rebuildTileMesh(paint.affectedTile);
    if (manager && manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    paint.needsRebuild = false;

    // Mouse-up: record the height-paint stroke as one undoable command.
    commitTerrainEdit();
}

void PanelTerrain::paintSplatAtWorldPos(const kVec3 &worldPos, bool erase)
{
    if (!manager || !manager->terrainManager ||
        paint.channel < 0 || paint.channel >= kTerrain::kMaxTerrainLayers)
        return;

    // Find the terrain tile under worldPos
    kTerrain *foundTile = nullptr;
    for (auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile || !tile->isLoaded())
            continue;

        int hx, hz;
        if (tile->worldToHeightmap(worldPos, hx, hz))
        {
            foundTile = tile;
            break;
        }
    }

    if (!foundTile)
        return;

    beginTerrainEdit(foundTile, false, true);

    const float radius = sculpt.radius;
    int res = foundTile->getHeightRes();

    int cx, cz;
    if (!foundTile->worldToHeightmap(worldPos, cx, cz))
        return;

    float sampleSpacing = foundTile->getWorldSize() / static_cast<float>(res - 1);
    int radSamples = static_cast<int>(std::ceil(radius / sampleSpacing));
    int xMin = (std::max)(0, cx - radSamples);
    int xMax = (std::min)(res - 1, cx + radSamples);
    int zMin = (std::max)(0, cz - radSamples);
    int zMax = (std::min)(res - 1, cz + radSamples);

    float worldRadiusSq = radius * radius;
    int channel = paint.channel;
    const int L = kTerrain::kMaxTerrainLayers;

    for (int z = zMin; z <= zMax; ++z)
    {
        for (int x = xMin; x <= xMax; ++x)
        {
            kVec3 sampleWorld = foundTile->heightmapToWorld(x, z);
            float dx = sampleWorld.x - worldPos.x;
            float dz = sampleWorld.z - worldPos.z;
            float distSq = dx * dx + dz * dz;

            if (distSq > worldRadiusSq)
                continue;

            float dist = std::sqrt(distSq);
            float t = dist / radius;
            float falloff = std::cos(t * 3.14159265358979323846f * 0.5f) * paint.strength;

            if (erase)
            {
                // Reduce the selected layer, redistribute evenly to the others.
                float oldVal = foundTile->getWeight(x, z, channel) / 255.0f;
                float reduction = oldVal * falloff;
                foundTile->setWeight(x, z, channel,
                    static_cast<unsigned char>((std::max)(0, static_cast<int>(roundf((oldVal - reduction) * 255.0f)))));

                float addEach = reduction / static_cast<float>(L - 1);
                for (int c = 0; c < L; ++c)
                {
                    if (c == channel)
                        continue;
                    float ov = foundTile->getWeight(x, z, c) / 255.0f;
                    float nv = ov + addEach;
                    foundTile->setWeight(x, z, c,
                        static_cast<unsigned char>((std::min)(255, static_cast<int>(roundf(nv * 255.0f)))));
                }
            }
            else
            {
                // Paint the selected layer, reduce the others proportionally.
                float current = foundTile->getWeight(x, z, channel) / 255.0f;
                float remaining = 1.0f - current;
                float add = remaining * falloff;
                float newVal = current + add;
                foundTile->setWeight(x, z, channel,
                    static_cast<unsigned char>((std::min)(255, static_cast<int>(roundf(newVal * 255.0f)))));

                float totalOther = 0.0f;
                for (int c = 0; c < L; ++c)
                    if (c != channel)
                        totalOther += foundTile->getWeight(x, z, c) / 255.0f;

                if (totalOther > 0.001f)
                {
                    float scale = (1.0f - newVal) / totalOther;
                    for (int c = 0; c < L; ++c)
                    {
                        if (c == channel)
                            continue;
                        float ov = foundTile->getWeight(x, z, c) / 255.0f;
                        int v = static_cast<int>(roundf(ov * scale * 255.0f));
                        foundTile->setWeight(x, z, c,
                            static_cast<unsigned char>((std::max)(0, (std::min)(255, v))));
                    }
                }
            }
        }
    }

    // Update GPU splat texture for live visual feedback
    foundTile->updateSplatTexture();

    paint.affectedTile = foundTile;
    paint.needsRebuild = true;
}

void PanelTerrain::rebuildPaintedSplatTile()
{
    if (!paint.needsRebuild || !paint.affectedTile)
        return;

    // No geometry changed — just ensure splat texture is up to date
    paint.affectedTile->updateSplatTexture();
    if (manager && manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    paint.needsRebuild = false;

    // Mouse-up: record the splat-paint stroke as one undoable command.
    commitTerrainEdit();
}

void PanelTerrain::fillLayerWeight(kTerrain *tile, int layer, unsigned char value)
{
    if (!tile || layer < 0 || layer >= kTerrain::kMaxTerrainLayers)
        return;

    // Snapshot the weight map so the whole fill is one undo step.
    beginTerrainEdit(tile, false, true);

    const int res = tile->getHeightRes();
    for (int z = 0; z < res; ++z)
        for (int x = 0; x < res; ++x)
            tile->setWeight(x, z, layer, value);

    tile->updateSplatTexture();
    tile->saveOwnedAssets();
    if (manager)
        manager->projectSaved = false;

    commitTerrainEdit();
}

// --- Noise helpers ----------------------------------------------------------

float PanelTerrain::noiseValue(int x, int y, int seed)
{
    unsigned int h = static_cast<unsigned int>(seed);
    h = ((h + static_cast<unsigned int>(x) * 374761393u) ^
         (static_cast<unsigned int>(y) * 668265263u)) *
        3284157443u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h) / 4294967295.0f * 2.0f - 1.0f;
}

float PanelTerrain::noiseSmooth(float x, float y, int seed)
{
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    float fx = x - std::floor(x);
    float fy = y - std::floor(y);

    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);

    float n00 = noiseValue(ix + 0, iy + 0, seed);
    float n10 = noiseValue(ix + 1, iy + 0, seed);
    float n01 = noiseValue(ix + 0, iy + 1, seed);
    float n11 = noiseValue(ix + 1, iy + 1, seed);

    float nx0 = n00 + (n10 - n00) * sx;
    float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}

void PanelTerrain::applyNoiseToTile(kTerrain *tile, float scale, float amplitude,
                                    int octaves, int seed, bool additive)
{
    if (!tile)
        return;

    int res = tile->getHeightRes();
    float *data = tile->getHeightData();

    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            float nx = static_cast<float>(x) / scale;
            float nz = static_cast<float>(z) / scale;

            float height = 0.0f;
            float freq = 1.0f;
            float amp = 1.0f;
            float maxAmp = 0.0f;

            // Rotate the sample coordinates between octaves so the fractal
            // lattice never lines up on the X/Z axes (a TerrainTools-style trick
            // that removes the grid-aligned ridges plain fBm produces).
            const float ca = 0.8f, sa = 0.6f;

            for (int oct = 0; oct < octaves; ++oct)
            {
                float n = noiseSmooth(nx * freq, nz * freq, seed + oct * 31337);

                // Fractal variant: 0 = fBm, 1 = ridged, 2 = billowed.
                if (m_noiseType == 1)
                {
                    n = 1.0f - std::fabs(n); // ridged: sharp mountain crests
                    n *= n;
                }
                else if (m_noiseType == 2)
                {
                    n = std::fabs(n);        // billowed: rounded, cloud-like
                }

                height += n * amp;
                maxAmp += amp;

                // Inter-octave rotation.
                float rx = nx * ca - nz * sa;
                float rz = nx * sa + nz * ca;
                nx = rx;
                nz = rz;

                freq *= 2.0f;
                amp *= 0.5f;
            }

            height /= maxAmp;
            height *= amplitude;

            int idx = z * res + x;
            if (additive)
                data[idx] += height;
            else
                data[idx] = height;
        }
    }

    rebuildTileMesh(tile);
}

// --- Erosion ----------------------------------------------------------------
// Hydraulic droplet erosion adapted from weigert/SimpleErosion: each droplet
// walks the heightfield along its (inertia-smoothed) gradient, scraping material
// from a circular brush and dropping it when it slows down or climbs, which
// carves valleys and river channels with natural-looking deposits.
void PanelTerrain::applyErosionToTile(kTerrain *tile, int droplets, float radius,
                                      float inertia, float capacityFactor,
                                      float depositFactor, float erodeFactor,
                                      float evaporate, float gravity, float minSediment,
                                      unsigned int seed)
{
    if (!tile)
        return;

    int res = tile->getHeightRes();
    float *data = tile->getHeightData();
    if (!data || res < 3)
        return;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Precompute the circular erosion brush footprint once.
    struct BrushSample { int dx, dz; float w; };
    std::vector<BrushSample> brush;
    int r = (std::max)(1, static_cast<int>(std::round(radius)));
    float wsum = 0.0f;
    for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx)
        {
            float d = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (d > radius)
                continue;
            float w = 1.0f - d / radius;
            brush.push_back({dx, dz, w});
            wsum += w;
        }
    if (brush.empty() || wsum <= 0.0f)
        return;
    for (BrushSample &b : brush)
        b.w /= wsum;

    // Bilinear height read in cell coordinates.
    auto sample = [&](float fx, float fz) -> float
    {
        int x0 = static_cast<int>(std::floor(fx));
        int z0 = static_cast<int>(std::floor(fz));
        float tx = fx - static_cast<float>(x0);
        float tz = fz - static_cast<float>(z0);
        int x1 = (std::min)(x0 + 1, res - 1);
        int z1 = (std::min)(z0 + 1, res - 1);
        x0 = (std::max)(0, (std::min)(x0, res - 1));
        z0 = (std::max)(0, (std::min)(z0, res - 1));
        float h00 = data[z0 * res + x0];
        float h10 = data[z0 * res + x1];
        float h01 = data[z1 * res + x0];
        float h11 = data[z1 * res + x1];
        float a = h00 + (h10 - h00) * tx;
        float b = h01 + (h11 - h01) * tx;
        return a + (b - a) * tz;
    };

    const float maxCoord = static_cast<float>(res - 1);

    for (int n = 0; n < droplets; ++n)
    {
        float px = unit(rng) * maxCoord;
        float pz = unit(rng) * maxCoord;
        float dirx = 0.0f, dirz = 0.0f;
        float speed = 1.0f, water = 1.0f, sediment = 0.0f;

        for (int life = 0; life < 30; ++life)
        {
            int cx = static_cast<int>(px);
            int cz = static_cast<int>(pz);
            if (cx < 0 || cz < 0 || cx >= res - 1 || cz >= res - 1)
                break;

            float hL = sample(px - 1.0f, pz);
            float hR = sample(px + 1.0f, pz);
            float hD = sample(px, pz - 1.0f);
            float hU = sample(px, pz + 1.0f);
            float gx = (hR - hL) * 0.5f;
            float gz = (hU - hD) * 0.5f;

            dirx = dirx * inertia - gx * (1.0f - inertia);
            dirz = dirz * inertia - gz * (1.0f - inertia);
            float len = std::sqrt(dirx * dirx + dirz * dirz);
            if (len < 1e-6f)
            {
                float a = unit(rng) * 6.2831853f;
                dirx = std::cos(a);
                dirz = std::sin(a);
            }
            else
            {
                dirx /= len;
                dirz /= len;
            }

            float nxp = px + dirx;
            float nzp = pz + dirz;
            if (nxp < 0.0f || nzp < 0.0f || nxp >= maxCoord || nzp >= maxCoord)
                break;

            float hOld = sample(px, pz);
            float hNew = sample(nxp, nzp);
            float dh = hNew - hOld;

            float cap = (std::max)(-dh * speed * water * capacityFactor, minSediment);

            if (sediment > cap || dh > 0.0f)
            {
                // Deposit at the current cell (drop what no longer fits).
                float amount = (dh > 0.0f)
                                   ? (std::min)(dh, sediment)
                                   : (sediment - cap) * depositFactor;
                sediment -= amount;
                int dx = (std::max)(0, (std::min)(static_cast<int>(std::round(px)), res - 1));
                int dz = (std::max)(0, (std::min)(static_cast<int>(std::round(pz)), res - 1));
                data[dz * res + dx] += amount;
            }
            else
            {
                // Erode from the brush footprint.
                float amount = (std::min)((cap - sediment) * erodeFactor, -dh);
                int ix = static_cast<int>(px);
                int iz = static_cast<int>(pz);
                for (const BrushSample &b : brush)
                {
                    int ex = ix + b.dx;
                    int ez = iz + b.dz;
                    if (ex < 0 || ez < 0 || ex >= res || ez >= res)
                        continue;
                    data[ez * res + ex] -= amount * b.w;
                }
                sediment += amount;
            }

            speed = std::sqrt((std::max)(0.0f, speed * speed + dh * gravity));
            water *= (1.0f - evaporate);
            px = nxp;
            pz = nzp;
            if (water < 0.01f)
                break;
        }
    }

    rebuildTileMesh(tile);
}

// Thermal (talus) erosion adapted from weigert/SoilMachine: any slope steeper
// than the talus angle sheds material to its lower neighbours until it rests.
void PanelTerrain::applyThermalErosionToTile(kTerrain *tile, int iterations, float talus, float strength)
{
    if (!tile)
        return;

    int res = tile->getHeightRes();
    float *data = tile->getHeightData();
    if (!data || res < 3)
        return;

    std::vector<float> delta(static_cast<size_t>(res) * res, 0.0f);
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int it = 0; it < iterations; ++it)
    {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int z = 1; z < res - 1; ++z)
        {
            for (int x = 1; x < res - 1; ++x)
            {
                float h = data[z * res + x];
                for (int k = 0; k < 4; ++k)
                {
                    int nx = x + nb[k][0];
                    int nz = z + nb[k][1];
                    float d = h - data[nz * res + nx];
                    if (d <= talus)
                        continue;
                    float move = (d - talus) * strength * 0.25f;
                    delta[z * res + x] -= move;
                    delta[nz * res + nx] += move;
                }
            }
        }
        for (size_t i = 0; i < delta.size(); ++i)
            data[i] += delta[i];
    }

    rebuildTileMesh(tile);
}

// Directional wind erosion adapted from weigert/SimpleWindErosion: steep faces
// expose material to the wind, which is carried one cell downwind per iteration.
void PanelTerrain::applyWindErosionToTile(kTerrain *tile, float dirRadians, float strength,
                                          int iterations, float talus)
{
    if (!tile)
        return;

    int res = tile->getHeightRes();
    float *data = tile->getHeightData();
    if (!data || res < 3)
        return;

    int ox = static_cast<int>(std::round(std::cos(dirRadians)));
    int oz = static_cast<int>(std::round(std::sin(dirRadians)));
    if (ox == 0 && oz == 0)
        ox = 1;

    std::vector<float> delta(static_cast<size_t>(res) * res, 0.0f);

    for (int it = 0; it < iterations; ++it)
    {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int z = 0; z < res; ++z)
        {
            for (int x = 0; x < res; ++x)
            {
                int ux = x - ox;
                int uz = z - oz;
                if (ux < 0 || uz < 0 || ux >= res || uz >= res)
                    continue;
                float diff = data[uz * res + ux] - data[z * res + x];
                if (diff <= talus)
                    continue;
                float move = (diff - talus) * strength * 0.5f;
                delta[uz * res + ux] -= move;
                delta[z * res + x] += move;
            }
        }
        for (size_t i = 0; i < delta.size(); ++i)
            data[i] += delta[i];
    }

    rebuildTileMesh(tile);
}

// Flow-accumulation river carving adapted from weigert/SimpleHydrology, erodr
// and FarazzShaikh/Terrain-Builder: build a D8 flow network, accumulate the
// upstream area draining through each cell, then incise it with the stream-power
// law (erosion ~ k * A^m * S^n). This produces branching river valleys and, with
// deposition, alluvial fans in the lowlands.
void PanelTerrain::applyHydrologyToTile(kTerrain *tile, int iterations, float k, float m, float n,
                                        float deposition, float minSlope)
{
    if (!tile)
        return;

    int res = tile->getHeightRes();
    float *data = tile->getHeightData();
    if (!data || res < 3)
        return;

    const size_t count = static_cast<size_t>(res) * res;
    std::vector<int>   order(count);
    std::vector<float> flow(count);
    std::vector<int>   recv(count);

    const int nb[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                          {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for (int it = 0; it < iterations; ++it)
    {
        // 1. Steepest-descent (D8) receiver for every cell.
        for (int z = 0; z < res; ++z)
        {
            for (int x = 0; x < res; ++x)
            {
                int i = z * res + x;
                float h = data[i];
                int best = -1;
                float bestSlope = 0.0f;
                for (int kk = 0; kk < 8; ++kk)
                {
                    int nx = x + nb[kk][0];
                    int nz = z + nb[kk][1];
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res)
                        continue;
                    float dh = h - data[nz * res + nx];
                    if (dh > bestSlope)
                    {
                        bestSlope = dh;
                        best = nz * res + nx;
                    }
                }
                recv[i] = best;
                flow[i] = 1.0f;
            }
        }

        // 2. Visit cells high -> low so upstream flow reaches each cell first.
        for (size_t i = 0; i < count; ++i)
            order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(),
                  [data](int a, int b) { return data[a] > data[b]; });
        for (int idx : order)
        {
            int r = recv[idx];
            if (r >= 0)
                flow[r] += flow[idx];
        }

        // 3. Stream-power incision.
        for (int z = 0; z < res; ++z)
        {
            for (int x = 0; x < res; ++x)
            {
                int i = z * res + x;
                int r = recv[i];
                if (r < 0)
                    continue;
                float slope = (std::max)(data[i] - data[r], minSlope);
                float e = k * std::pow(flow[i], m) * std::pow(slope, n);
                e = (std::min)(e, data[i] - data[r]); // never invert the slope
                if (e <= 0.0f)
                    continue;
                data[i] -= e;
                data[r] += e * deposition;
            }
        }
    }

    rebuildTileMesh(tile);
}

// ---------------------------------------------------------------------------
// Owned assets + non-destructive height layers
// ---------------------------------------------------------------------------

void PanelTerrain::setTerrainAssetDirectory(const kString &directory)
{
    if (manager && manager->terrainManager)
        manager->terrainManager->setAssetDirectory(directory);
}

void PanelTerrain::saveAllOwnedAssets()
{
    if (!manager || !manager->terrainManager)
        return;

    for (const auto &pair : manager->terrainManager->getTiles())
        if (kTerrain *tile = pair.second.get())
            tile->saveOwnedAssets();
}

PanelTerrain::LayerEditBuffer &PanelTerrain::layerEditBufferFor(
    kTerrain *tile, int index, const kTerrainHeightLayer &current)
{
    for (auto &buffer : m_layerEditBuffers)
        if (buffer.tile == tile && buffer.index == index)
            return buffer;

    m_layerEditBuffers.push_back({tile, index, current});
    return m_layerEditBuffers.back();
}

void PanelTerrain::clearLayerEditBuffers(kTerrain *tile)
{
    m_layerEditBuffers.erase(
        std::remove_if(m_layerEditBuffers.begin(), m_layerEditBuffers.end(),
                       [tile](const LayerEditBuffer &buffer) { return buffer.tile == tile; }),
        m_layerEditBuffers.end());
}

int PanelTerrain::applyHeightLayer(kTerrain *tile, int layerIndex,
                                   const kTerrainHeightLayer &desc,
                                   const std::function<void(const std::vector<float> &,
                                                            std::vector<float> &)> &generator)
{
    if (!tile || !manager)
        return -1;

    std::vector<kTerrainHeightLayer> &layers = tile->getHeightLayers();

    auto cmd = std::make_unique<TerrainEditCommand>();
    cmd->manager = manager;

    TerrainEditCommand::TileDiff diff;
    diff.tile = tile;
    diff.res = tile->getHeightRes();
    diff.hasLayer = true;

    if (layerIndex < 0)
    {
        // Appending: undo simply removes it again.
        layerIndex = tile->addHeightLayer(desc);
        diff.layerBeforeValid = false;
    }
    else
    {
        if (layerIndex >= static_cast<int>(layers.size()))
            return -1;
        diff.layerBeforeValid = true;
        diff.layerBefore = layers[static_cast<size_t>(layerIndex)];
    }

    diff.layerIndex = layerIndex;

    // A parameter/weight-only change keeps the existing offsets; a regenerate
    // replaces them wholesale.
    {
        const std::vector<float> keptOffsets = layers[static_cast<size_t>(layerIndex)].offsets;
        kTerrainHeightLayer updated = desc;
        if (generator == nullptr)
            updated.offsets = keptOffsets;
        layers[static_cast<size_t>(layerIndex)] = std::move(updated);
    }

    if (generator)
        tile->generateHeightLayer(layerIndex, generator);
    else
        tile->recomposeHeight();

    diff.layerAfterValid = true;
    diff.layerAfter = layers[static_cast<size_t>(layerIndex)];

    if (tile->isLoaded())
        rebuildTileMesh(tile);

    cmd->diffs.push_back(std::move(diff));
    manager->undoRedo.push(std::move(cmd));

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;

    // Persist the layer straight away: offsets are not cheap to regenerate.
    tile->saveOwnedAssets();

    // The stored layer is now authoritative, so drop the scratch copy: the
    // widgets reseed from the applied values on the next frame.
    m_layerEditBuffers.erase(
        std::remove_if(m_layerEditBuffers.begin(), m_layerEditBuffers.end(),
                       [tile, layerIndex](const LayerEditBuffer &buffer) {
                           return buffer.tile == tile && buffer.index == layerIndex;
                       }),
        m_layerEditBuffers.end());

    return layerIndex;
}

void PanelTerrain::removeHeightLayerUndoable(kTerrain *tile, int layerIndex)
{
    if (!tile || !manager)
        return;

    std::vector<kTerrainHeightLayer> &layers = tile->getHeightLayers();
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers.size()))
        return;

    auto cmd = std::make_unique<TerrainEditCommand>();
    cmd->manager = manager;

    TerrainEditCommand::TileDiff diff;
    diff.tile = tile;
    diff.res = tile->getHeightRes();
    diff.hasLayer = true;
    diff.layerIndex = layerIndex;
    diff.layerBeforeValid = true;
    diff.layerBefore = layers[static_cast<size_t>(layerIndex)];
    diff.layerAfterValid = false;

    tile->removeHeightLayer(layerIndex);
    if (tile->isLoaded())
        rebuildTileMesh(tile);

    cmd->diffs.push_back(std::move(diff));
    manager->undoRedo.push(std::move(cmd));

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;
    tile->saveOwnedAssets();

    // Removing a layer shifts every following index, so any in-progress edits for
    // this tile no longer line up — drop them.
    clearLayerEditBuffers(tile);
}

// A few passes of a 3x3 average over a square field. Per-sample operators leave
// single-sample steps behind, and once that field is drawn as a mesh those steps
// read as blocky facets — this relaxes them without flattening the large shapes.
static void smoothHeightField(std::vector<float> &data, int res, int passes)
{
    if (res < 3 || data.size() != static_cast<size_t>(res) * res)
        return;

    std::vector<float> tmp(data.size());
    for (int p = 0; p < passes; ++p)
    {
        for (int z = 0; z < res; ++z)
        {
            const int z0 = (std::max)(0, z - 1);
            const int z1 = (std::min)(res - 1, z + 1);
            for (int x = 0; x < res; ++x)
            {
                const int x0 = (std::max)(0, x - 1);
                const int x1 = (std::min)(res - 1, x + 1);

                float sum = 0.0f;
                for (int nz = z0; nz <= z1; ++nz)
                    for (int nx = x0; nx <= x1; ++nx)
                        sum += data[static_cast<size_t>(nz) * res + nx];

                const float count = static_cast<float>((z1 - z0 + 1) * (x1 - x0 + 1));
                tmp[static_cast<size_t>(z) * res + x] = sum / count;
            }
        }
        data.swap(tmp);
    }
}

std::function<void(const std::vector<float> &, std::vector<float> &)>
PanelTerrain::makeHeightLayerGenerator(kTerrain *tile, const kTerrainHeightLayer &layer)
{
    if (!tile)
        return nullptr;

    return [this, tile, layer](const std::vector<float> &input, std::vector<float> &output)
    {
        float *height = tile->getHeightData();
        if (!height || tile->getHeightDataSize() != input.size())
            return;

        // Every operator reads and writes the tile's height field in place, so seed
        // it with this layer's input, run the operator, then hand the result back.
        // kTerrain::generateHeightLayer turns that into the layer's delta.
        //
        // The operators also mutate the tile's own fields as a side effect (several
        // call rebuildMesh/stitchSeams, which recomposes base + layers). Snapshot the
        // sculpted base and every layer's offsets so generating a layer is a pure
        // function of its parameters — pressing Apply twice must produce the same
        // result, not stack the effect again.
        std::vector<float> savedBase;
        if (const float *basePtr = tile->getBaseHeightData())
            savedBase.assign(basePtr, basePtr + tile->getHeightDataSize());

        std::vector<std::vector<float>> savedOffsets;
        savedOffsets.reserve(tile->getHeightLayers().size());
        for (const auto &otherLayer : tile->getHeightLayers())
            savedOffsets.push_back(otherLayer.offsets);

        std::copy(input.begin(), input.end(), height);

        // The noise operator reads the fractal variant from the panel member, so
        // the layer's stored variant temporarily wins.
        const int savedNoiseType = m_noiseType;
        m_noiseType = layer.noiseType;

        switch (layer.type)
        {
        case kTerrainHeightLayerType::Noise:
            applyNoiseToTile(tile, layer.noiseScale, layer.noiseAmplitude,
                             layer.noiseOctaves, layer.noiseSeed, layer.noiseAdditive);
            break;
        case kTerrainHeightLayerType::HydraulicErosion:
            applyErosionToTile(tile, layer.droplets, layer.erodeRadius, layer.erodeInertia,
                               layer.erodeCapacity, layer.erodeDeposit, layer.erodeErode,
                               layer.erodeEvaporate, layer.erodeGravity, 0.01f,
                               static_cast<unsigned int>(layer.noiseSeed));
            break;
        case kTerrainHeightLayerType::ThermalErosion:
            applyThermalErosionToTile(tile, layer.iterations, layer.talus, layer.strength);
            break;
        case kTerrainHeightLayerType::WindErosion:
            applyWindErosionToTile(tile, layer.windDirection, layer.strength,
                                   layer.iterations, layer.talus);
            break;
        case kTerrainHeightLayerType::Hydrology:
            applyHydrologyToTile(tile, layer.iterations, layer.hydroK, layer.hydroM,
                                 layer.hydroN, layer.hydroDeposit, 0.01f);
            break;
        }

        m_noiseType = savedNoiseType;

        std::copy(height, height + input.size(), output.begin());

        // Undo the operator's side effects on the tile: the sculpted base and the
        // other layers must be exactly as they were before this sample was taken.
        if (!savedBase.empty())
            std::copy(savedBase.begin(), savedBase.end(), tile->getBaseHeightData());
        {
            std::vector<kTerrainHeightLayer> &layersNow = tile->getHeightLayers();
            const size_t count = (std::min)(layersNow.size(), savedOffsets.size());
            for (size_t i = 0; i < count; ++i)
                layersNow[i].offsets = savedOffsets[i];
        }

        // Relax the raw operator result before it becomes a layer delta, so the
        // composited terrain renders smooth instead of faceted.
        smoothHeightField(output, tile->getHeightRes(), 2);
    };
}

void PanelTerrain::regenerateHeightLayer(kTerrain *tile, int layerIndex)
{
    if (!tile)
        return;

    std::vector<kTerrainHeightLayer> &layers = tile->getHeightLayers();
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers.size()))
        return;

    // Copy first: applyHeightLayer replaces the element, which would dangle a
    // reference into the vector.
    const kTerrainHeightLayer desc = layers[static_cast<size_t>(layerIndex)];
    applyHeightLayer(tile, layerIndex, desc, makeHeightLayerGenerator(tile, desc));
}

void PanelTerrain::applyTerrainResize(float worldSize, int heightRes)
{
    if (!manager)
        return;

    m_settingsWorldSize = worldSize;
    m_settingsHeightRes = heightRes;

    // With no terrain manager yet there is nothing to resample, so the staged
    // values are simply applied once the scene gets its first tile.
    if (!manager->terrainManager)
    {
        m_applySizeOnCreate = true;
        return;
    }

    // Every tile resamples its height, weight map and modifier layers; quality is
    // intentionally not preserved (see kTerrainManager::resizeAllTiles).
    manager->terrainManager->resizeAllTiles(worldSize, heightRes);

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;
}

void PanelTerrain::applyTileHeight(kTerrain *tile)
{
    if (!tile || !manager)
        return;

    beginTerrainEdit(tile, true, false);

    // Settle the base field on the panel's tile height ...
    float *base = tile->getBaseHeightData();
    if (base)
        std::fill(base, base + tile->getHeightDataSize(), m_defaultHeight);

    // ... and clear the effect layers' height deltas so the tile reads exactly at
    // that height instead of being pushed away from it by a modifier.
    for (auto &layer : tile->getHeightLayers())
        std::fill(layer.offsets.begin(), layer.offsets.end(), 0.0f);

    tile->recomposeHeight();
    if (tile->isLoaded())
        rebuildTileMesh(tile);

    commitTerrainEdit();

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;

    // Persist straight away so the tile's .height file matches what is on screen.
    tile->saveOwnedAssets();
}

void PanelTerrain::clearSelectedTileHeight(kTerrain *tile)
{
    // Clearing to the tile height is the same operation as applying it.
    applyTileHeight(tile);
}

void PanelTerrain::removeTerrainLayer(kTerrain *tile, int index)
{
    if (!tile)
        return;

    auto &layers = tile->getLayers();
    if (index < 0 || index >= static_cast<int>(layers.size()))
        return;

    const int L = kTerrain::kMaxTerrainLayers;

    // Weights are packed per layer, so every layer above the removed one must move
    // down a slot or the painted masks would stay behind on the wrong material.
    // Layer add/remove is not part of the undo history (the undo command stores
    // height/weight blocks only), so no edit snapshot is taken here.
    const int res = tile->getHeightRes();
    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            for (int c = index; c < L - 1; ++c)
                tile->setWeight(x, z, c, tile->getWeight(x, z, c + 1));
            tile->setWeight(x, z, L - 1, 0);
        }
    }
    tile->updateSplatTexture();

    // "Mask by layer" references follow their layer.
    for (auto &layer : layers)
    {
        if (layer.maskChannel > index)
            --layer.maskChannel;
        else if (layer.maskChannel == index)
            layer.maskChannel = -1; // the mask layer is gone -> fall back to auto
    }

    layers.erase(layers.begin() + index);

    // In-progress per-layer edit buffers are keyed by index.
    clearLayerEditBuffers(tile);

    // Repack texture slots / uniforms (layer indices shifted) and refresh the mesh.
    tile->syncLayerTiling();
    tile->syncLayerMaskChannels();
    tile->rebuildLayerArrays();
    rebuildTileMesh(tile);
    tile->saveOwnedAssets();

    // Keep the paint target on a layer that still exists.
    if (paint.channel >= static_cast<int>(layers.size()))
        paint.channel = static_cast<int>(layers.size()) - 1;
    if (paint.channel < 0)
        paint.channel = 0;

    if (manager)
        manager->projectSaved = false;
}

void PanelTerrain::appendTileColliderLines(const kTerrain *tile,
                                           std::vector<kVec3> &out) const
{
    if (!tile)
        return;

    kMesh *mesh = tile->getMesh();
    const kPhysicsObjectDesc *desc =
        (mesh && mesh->getHasPhysicsDesc()) ? &mesh->getPhysicsDesc() : nullptr;

    // Prefer the generated collider's own resampled grid: that is the grid Jolt
    // fills the HeightFieldShape with, so the wireframe then reflects the
    // resolution the Physics slider actually produced. Drawing a fixed decimation
    // of the height grid instead is why changing the resolution looked like it did
    // nothing at all.
    if (desc && desc->shape.type == kPhysicsShapeType::HeightField &&
        desc->shape.heightSampleCount >= 2 &&
        desc->shape.heightSamples.size() ==
            static_cast<size_t>(desc->shape.heightSampleCount) *
                static_cast<size_t>(desc->shape.heightSampleCount))
    {
        const int n = desc->shape.heightSampleCount;
        const kVec3 scale = desc->shape.heightScale;
        const kVec3 offset = desc->shape.offset;
        const kVec3 origin = mesh->getGlobalPosition();

        auto colliderPoint = [&](int x, int z)
        {
            const float h = desc->shape.heightSamples[static_cast<size_t>(z) * n + x];
            return origin + kVec3(offset.x + static_cast<float>(x) * scale.x,
                                  offset.y + h * scale.y,
                                  offset.z + static_cast<float>(z) * scale.z);
        };

        // Decimation must never make a COARSER collider draw more lines than a
        // finer one (the old n/40 stride drew all 79 samples of a 79-sample
        // collider but only every 12th of a 512-sample one). The cell budget grows
        // slowly with the resolution instead, so density is monotonic and a
        // 512-sample collider stays at ~64x64 debug cells.
        const int maxCells = (std::min)(64, 16 + n / 8);
        const int stride = (std::max)(1, (n + maxCells - 1) / maxCells);
        for (int z = 0; z < n; z += stride)
        {
            for (int x = 0; x < n; x += stride)
            {
                const kVec3 a = colliderPoint(x, z);
                const int nx = (std::min)(x + stride, n - 1);
                const int nz = (std::min)(z + stride, n - 1);
                if (nx != x)
                {
                    out.push_back(a);
                    out.push_back(colliderPoint(nx, z));
                }
                if (nz != z)
                {
                    out.push_back(a);
                    out.push_back(colliderPoint(x, nz));
                }
            }
        }

        if (!out.empty())
            return;
    }

    // Fallback: no collider samples yet — outline the height grid itself.
    const int res = tile->getHeightRes();
    const float *heights = tile->getHeightData();
    if (!heights || res < 2)
        return;

    const int last = res - 1;

    // Decimate it so a full-resolution tile (hundreds of samples per side) does not
    // flood the debug-line buffer every frame.
    const int stride = (std::max)(1, res / 32);

    auto pointAt = [&](int x, int z)
    {
        kVec3 p = tile->heightmapToWorld(x, z);
        p.y = heights[static_cast<size_t>(z) * res + x];
        return p;
    };

    for (int z = 0; z <= last; z += stride)
    {
        for (int x = 0; x <= last; x += stride)
        {
            const kVec3 a = pointAt(x, z);
            if (x + stride <= last)
            {
                out.push_back(a);
                out.push_back(pointAt(x + stride, z));
            }
            if (z + stride <= last)
            {
                out.push_back(a);
                out.push_back(pointAt(x, z + stride));
            }
        }
    }
}

void PanelTerrain::generateTileCollider(kTerrain *tile, int resolution)
{
    if (!tile || !manager)
        return;

    kMesh *mesh = tile->getMesh();
    if (!mesh)
        return;

    const int res = tile->getHeightRes();
    const float *heights = tile->getHeightData();
    if (!heights || res < 2)
        return;

    const int target = (std::max)(2, (std::min)(resolution, 4096));

    kObject *obj = static_cast<kObject *>(mesh);
    kPhysicsObjectDesc &desc = obj->getPhysicsDesc();
    desc.shape.type = kPhysicsShapeType::HeightField;
    desc.type = kPhysicsObjectType::Static;
    desc.layer = "Default";

    // Resample the tile's height grid to the requested collider resolution so the
    // shape's triangle count follows the input (higher = finer, more accurate).
    desc.shape.heightSamples.resize(static_cast<size_t>(target) * target);
    for (int z = 0; z < target; ++z)
    {
        const float fz = (target > 1) ? (static_cast<float>(z) / (target - 1)) * (res - 1) : 0.0f;
        const int z0 = static_cast<int>(fz);
        const int z1 = (std::min)(z0 + 1, res - 1);
        const float tz = fz - static_cast<float>(z0);
        for (int x = 0; x < target; ++x)
        {
            const float fx = (target > 1) ? (static_cast<float>(x) / (target - 1)) * (res - 1) : 0.0f;
            const int x0 = static_cast<int>(fx);
            const int x1 = (std::min)(x0 + 1, res - 1);
            const float tx = fx - static_cast<float>(x0);

            const float h00 = heights[static_cast<size_t>(z0) * res + x0];
            const float h10 = heights[static_cast<size_t>(z0) * res + x1];
            const float h01 = heights[static_cast<size_t>(z1) * res + x0];
            const float h11 = heights[static_cast<size_t>(z1) * res + x1];
            const float h0 = h00 + (h10 - h00) * tx;
            const float h1 = h01 + (h11 - h01) * tx;
            desc.shape.heightSamples[static_cast<size_t>(z) * target + x] = h0 + (h1 - h0) * tz;
        }
    }
    desc.shape.heightSampleCount = target;

    const float world = tile->getWorldSize();
    const float spacing = world / static_cast<float>(target - 1);
    desc.shape.heightScale = kVec3(spacing, 1.0f, spacing);
    // The mesh is centred on the tile origin, so sample (0,0) is the (-half,-half)
    // corner in local space.
    desc.shape.offset = kVec3(-world * 0.5f, 0.0f, -world * 0.5f);

    obj->setHasPhysicsDesc(true);
    manager->projectSaved = false;
}

void PanelTerrain::rebuildTileMesh(kTerrain *tile)
{
    if (!tile)
        return;

    kMesh *oldMesh = tile->getMesh();

    tile->rebuildMesh(true);

    kMesh *newMesh = tile->getMesh();
    if (manager && oldMesh && newMesh && oldMesh != newMesh)
    {
        // kTerrain::rebuildMesh() deleted the previous kMesh, so any raw pointer the
        // editor still holds to it (the active selection) now dangles. Re-point the
        // selection at the rebuilt mesh; its UUID is preserved, so the uuid lists
        // the Hierarchy uses stay valid.
        kObject *&activeSelection = manager->getActiveSelectedObject();
        if (activeSelection == oldMesh)
            activeSelection = newMesh;
        if (manager->selectedObject == oldMesh)
            manager->selectedObject = newMesh;
    }

    // rebuildMesh(true) re-stitched this tile's boundary against the neighbours, so
    // their meshes now hold stale edge samples. Refresh them to keep the seam
    // closed; the flag stops rebuildNeighborMeshes() -> rebuildTileMesh() from
    // recursing back into this tile.
    if (!m_rebuildingMesh)
    {
        m_rebuildingMesh = true;
        rebuildNeighborMeshes(tile->getGridX(), tile->getGridZ());
        m_rebuildingMesh = false;
    }
}

void PanelTerrain::drawTexturePickerPopup()
{
    if (!m_texPickOpen)
        return;

    // The picker is a self-managed top-level window instead of an ImGui popup.
    // A popup was dismissed by clicks that did not target it (scrollbar drags,
    // the empty area around the grid) and by the raw SDL input the viewport
    // consumes before ImGui refreshes its capture state. Here only Select,
    // Cancel or the close button dismiss it.
    const ImGuiViewport *vp = ImGui::GetMainViewport();

    // Full-viewport blocker: a click anywhere outside the picker lands on this
    // window instead of reaching the editor behind it. NoBringToFrontOnFocus keeps
    // it underneath the picker even when it is clicked.
    {
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##terrainTexBlocker", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, 480.0f), ImGuiCond_Appearing);

    bool stayOpen = true;
    bool doApply = false;

    if (ImGui::Begin("Select Terrain Texture###terrainTexPicker", &stayOpen,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::IsWindowAppearing())
        {
            // Keep the picker above the blocker and focus the search field.
            ImGui::SetWindowFocus();
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##tertexsearch", "Search...", m_texPickSearch,
                                 sizeof(m_texPickSearch));

    std::string filter = m_texPickSearch;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Every texture asset in the project, sorted by display name.
    std::vector<std::pair<std::string, kString>> textures; // {display name, uuid}
    if (manager)
    {
        for (const auto &kv : manager->fileMap)
        {
            if (kv.second.type != "image")
                continue;
            textures.emplace_back(fs::path(kv.second.path).stem().string(), kv.first);
        }
        std::sort(textures.begin(), textures.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
    ImGui::BeginChild("##tertexgrid", ImVec2(0.0f, -footer), true);
    {
        const float thumb = 72.0f;
        const float cellW = thumb + 12.0f;
        const float cellH = thumb + ImGui::GetTextLineHeightWithSpacing() + 6.0f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const int cols = (std::max)(1, static_cast<int>(availW / cellW));
        int col = 0;

        // One thumbnail tile: framed preview + centred, ellipsized name.
        auto cell = [&](const char *itemLabel, const kString &uuid, bool none)
        {
            ImGui::PushID(none ? "##none" : uuid.c_str());
            const bool selected = none ? m_texPickSelected.empty()
                                       : (!m_texPickSelected.empty() &&
                                          m_texPickSelected == uuid);

            const ImVec2 cur0 = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##cell", selected, ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(cellW - 4.0f, cellH)))
            {
                m_texPickSelected = none ? kString() : uuid;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    doApply = true;
            }

            ImDrawList *dl = ImGui::GetWindowDrawList();
            const float ix = cur0.x + (cellW - 4.0f - thumb) * 0.5f;
            const float iy = cur0.y + 3.0f;

            if (none)
            {
                dl->AddRect(ImVec2(ix, iy), ImVec2(ix + thumb, iy + thumb),
                            IM_COL32(120, 120, 130, 180));
            }
            else
            {
                ImTextureRef img;
                if (manager && manager->panelProject)
                    img = manager->panelProject->getThumbnailIcon(uuid, ImTextureRef());
                if (img._TexData != nullptr || img._TexID != ImTextureID_Invalid)
                    dl->AddImage(img, ImVec2(ix, iy), ImVec2(ix + thumb, iy + thumb));
                else
                    dl->AddRect(ImVec2(ix, iy), ImVec2(ix + thumb, iy + thumb),
                                IM_COL32(120, 120, 130, 180));
            }

            const std::string nm = fitTextWithEllipsisUtf8(gui, itemLabel, cellW - 6.0f);
            const float tw = ImGui::CalcTextSize(nm.c_str()).x;
            dl->AddText(ImVec2(cur0.x + (cellW - 4.0f - tw) * 0.5f, iy + thumb + 3.0f),
                        ImGui::GetColorU32(ImGuiCol_Text), nm.c_str());

            ImGui::PopID();
            if (++col < cols)
                ImGui::SameLine();
            else
                col = 0;
        };

        cell("(None)", kString(), true);
        for (const auto &tex : textures)
        {
            if (!filter.empty())
            {
                std::string lower = tex.first;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c)
                               { return static_cast<char>(std::tolower(c)); });
                if (lower.find(filter) == std::string::npos)
                    continue;
            }
            cell(tex.first.c_str(), tex.second, false);
        }
    }
    ImGui::EndChild();

        if (ImGui::Button("Select", ImVec2(120.0f, 0.0f)))
            doApply = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            stayOpen = false;

        // Esc dismisses the picker, matching a modal.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            stayOpen = false;
    }
    ImGui::End();

    if (doApply)
    {
        if (m_texPickTile && m_texPickLayer >= 0 && manager)
        {
            auto &layers = m_texPickTile->getLayers();
            if (m_texPickLayer < static_cast<int>(layers.size()))
            {
                kString newPath;
                if (!m_texPickSelected.empty())
                {
                    auto it = manager->fileMap.find(m_texPickSelected);
                    if (it != manager->fileMap.end())
                        newPath = (manager->projectPath / "Assets" / it->second.path).string();
                }

                kTerrainLayer &layer = layers[static_cast<size_t>(m_texPickLayer)];
                if (m_texPickSlot == 0)
                    layer.albedoPath = newPath;
                else if (m_texPickSlot == 1)
                    layer.normalPath = newPath;
                else
                    layer.ormPath = newPath;

                // Repack the layer texture arrays so the change shows immediately.
                m_texPickTile->rebuildLayerArrays();
                rebuildTileMesh(m_texPickTile);
                m_texPickTile->saveOwnedAssets();
                manager->projectSaved = false;
            }
        }
        stayOpen = false;
    }

    if (!stayOpen)
    {
        m_texPickOpen = false;
        m_texPickTile = nullptr;
        m_texPickLayer = -1;
    }
}

kTerrain *PanelTerrain::getSelectedTerrainTile(kTerrainManager *terrainMgr)
{
    if (!terrainMgr || !manager)
        return nullptr;

    // Singular active object first (set by picking / creation) ...
    if (kObject *sel = manager->getActiveSelectedObject())
    {
        for (const auto &pair : terrainMgr->getTiles())
        {
            kTerrain *tile = pair.second.get();
            kMesh *tileMesh = tile ? tile->getMesh() : nullptr;
            if (!tileMesh)
                continue;

            if (dynamic_cast<kMesh *>(sel) == tileMesh)
                return tile;
            if (!sel->getUuid().empty() && tileMesh->getUuid() == sel->getUuid())
                return tile;
        }
    }

    // ... then the plural UUID list, which is what the Hierarchy actually updates
    // when a row is clicked. Without this a Hierarchy selection resolved to
    // nothing and the panel looked empty.
    const std::vector<kString> &selectedUuids = manager->getActiveSelectedObjects();
    for (const auto &pair : terrainMgr->getTiles())
    {
        kTerrain *tile = pair.second.get();
        kMesh *tileMesh = tile ? tile->getMesh() : nullptr;
        if (!tileMesh || tileMesh->getUuid().empty())
            continue;

        if (std::find(selectedUuids.begin(), selectedUuids.end(),
                      tileMesh->getUuid()) != selectedUuids.end())
            return tile;
    }

    return nullptr;
}

void PanelTerrain::drawTerrainSizeSettings(kTerrainManager *terrainMgr)
{
    // Part of the "Terrain Tile Settings" section: changing either value resamples
    // every tile's height map, splat map and height modifier layers, so it is
    // staged here and only applied after an explicit confirmation.
    ImGui::TextDisabled("Tile size / resolution (applies to every tile):");

    if (m_settingsWorldSize <= 0.0f && terrainMgr)
        m_settingsWorldSize = terrainMgr->getWorldSize();
    if (m_settingsHeightRes < 2 && terrainMgr)
        m_settingsHeightRes = terrainMgr->getHeightRes();

    ImGui::DragFloat("Tile World Size", &m_settingsWorldSize, 1.0f, 8.0f, 8192.0f, "%.1f");
    ImGui::DragInt("Height Resolution", &m_settingsHeightRes, 1, 33, 4097);

    if (ImGui::Button("Save", ImVec2(-1, 0)))
    {
        const float currentWorld = terrainMgr ? terrainMgr->getWorldSize() : 256.0f;
        const int currentRes = terrainMgr ? terrainMgr->getHeightRes() : 513;

        if (terrainMgr &&
            m_settingsWorldSize == currentWorld && m_settingsHeightRes == currentRes)
        {
            m_messageBoxText = "Terrain size and resolution are unchanged.";
            m_messageBoxPending = true;
        }
        else
        {
            m_pendingWorldSize = m_settingsWorldSize;
            m_pendingHeightRes = m_settingsHeightRes;
            m_resizeConfirmPending = true;
        }
    }

    if (m_resizeConfirmPending)
    {
        ImGui::OpenPopup("Resize Terrain##confirm");
        m_resizeConfirmPending = false;
    }

    if (ImGui::BeginPopupModal("Resize Terrain##confirm", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Change terrain size / resolution?");
        ImGui::Separator();
        ImGui::Text("World size : %.1f  ->  %.1f",
                    terrainMgr ? terrainMgr->getWorldSize() : 0.0f, m_pendingWorldSize);
        ImGui::Text("Resolution : %d  ->  %d",
                    terrainMgr ? terrainMgr->getHeightRes() : 0, m_pendingHeightRes);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
        ImGui::TextWrapped("Every tile's height map, splat map and height modifier layers "
                           "will be resampled to the new resolution. Detail will not be "
                           "preserved.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (ImGui::Button("Confirm", ImVec2(120, 0)))
        {
            applyTerrainResize(m_pendingWorldSize, m_pendingHeightRes);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            // Snap the staged values back to what is actually in use.
            if (terrainMgr)
            {
                m_settingsWorldSize = terrainMgr->getWorldSize();
                m_settingsHeightRes = terrainMgr->getHeightRes();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
}

void PanelTerrain::draw(bool &isOpened)
{
    if (!isOpened)
        return;

    // Re-attach the panel to the dock node it occupied when the layout was last
    // saved. Applied only once (and cleared immediately after) so the user can
    // still freely undock or move the panel afterwards.
    if (terrainDockRestore && terrainDockId != 0)
    {
        ImGui::SetNextWindowDockID(terrainDockId, ImGuiCond_Always);
        terrainDockRestore = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 650), ImGuiCond_FirstUseEver);
    bool windowOpen = ImGui::Begin("Terrain", &isOpened);

    // Remember where the panel is currently attached so File > Save Project can
    // persist the dock node and restore it when the project is reopened.
    if (ImGui::GetWindowDockID() != 0)
        terrainDockId = ImGui::GetWindowDockID();

    if (!windowOpen)
    {
        ImGui::End();
        return;
    }

    // Deferred info popup used by the Tile Management controls.
    if (m_messageBoxPending)
    {
        ImGui::OpenPopup("Terrain Message");
        m_messageBoxPending = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Terrain Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_messageBoxText.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    kWorld *world = manager->getWorld();
    kScene *scene = manager->getScene();
    kTerrainManager *terrainMgr = manager->terrainManager;

    if (!world || !scene)
    {
        ImGui::TextDisabled("No scene loaded.");
        ImGui::End();
        return;
    }

    // A scene holds at most one terrain. Detect whether the active scene already
    // contains one (via the terrain manager, or by scanning the scene graph).
    bool sceneHasTerrain = (terrainMgr && terrainMgr->getTotalCount() > 0);
    if (!sceneHasTerrain && scene->getRootNode())
    {
        std::function<void(kObject *)> scan = [&](kObject *n)
        {
            if (sceneHasTerrain || !n)
                return;
            kMesh *m = dynamic_cast<kMesh *>(n);
            if (m && m->getSerializeType() == "terrain")
            {
                sceneHasTerrain = true;
                return;
            }
            for (kObject *c : n->getChildren())
            {
                if (sceneHasTerrain)
                    break;
                scan(c);
            }
        };
        for (kObject *c : scene->getRootNode()->getChildren())
        {
            if (sceneHasTerrain)
                break;
            scan(c);
        }
    }

    // Is the current selection a terrain tile (or the scene that owns one)?
    //
    // The editor keeps two selections: the singular `getActiveSelectedObject()`
    // (set when an object is picked/created) and the plural
    // `getActiveSelectedObjects()` UUID list that the Hierarchy actually updates
    // when a row is clicked. Both must be consulted, which is why selecting a
    // terrain tile from the Hierarchy previously showed nothing here.
    bool terrainSelected = false;

    if (kObject *sel = manager->getActiveSelectedObject())
    {
        if (sel->getType() == kNodeType::NODE_TYPE_TERRAIN)
            terrainSelected = true;
        else if (kMesh *sm = dynamic_cast<kMesh *>(sel))
            terrainSelected = (sm->getSerializeType() == "terrain");
    }

    if (!terrainSelected && terrainMgr)
    {
        const std::vector<kString> &selectedUuids = manager->getActiveSelectedObjects();
        for (const auto &pair : terrainMgr->getTiles())
        {
            kTerrain *tile = pair.second.get();
            kMesh *tileMesh = tile ? tile->getMesh() : nullptr;
            if (!tileMesh)
                continue;

            const kString &uuid = tileMesh->getUuid();
            if (uuid.empty())
                continue;

            if (std::find(selectedUuids.begin(), selectedUuids.end(), uuid) !=
                selectedUuids.end())
            {
                terrainSelected = true;
                break;
            }
        }
    }

    bool sceneSelected = (manager->selectedScene == scene);

    // Show only "Create New Terrain" until the scene has a terrain; afterwards
    // show the full editor only while the scene or a terrain tile is selected.
    bool showFullUi = (terrainMgr != nullptr) &&
                      (terrainSelected || (sceneSelected && sceneHasTerrain));

    if (!showFullUi)
    {
        if (!sceneHasTerrain)
        {
            // No terrain in this scene yet: only Tile Management and the size /
            // resolution settings are meaningful. Everything else — sculpting,
            // painting, height layers, material, file operations — stays hidden.
            ImGui::TextDisabled("This scene has no terrain yet.");
            ImGui::Spacing();

            ImGui::SeparatorText("Terrain Tile Settings");
            if (ImGui::Button("Add Terrain to Scene", ImVec2(-1, 0)))
            {
                manager->createTerrain();
                // A size / resolution confirmed before any tile existed is applied
                // to the manager now that it exists.
                if (m_applySizeOnCreate && manager->terrainManager)
                {
                    manager->terrainManager->resizeAllTiles(m_settingsWorldSize,
                                                            m_settingsHeightRes);
                    m_applySizeOnCreate = false;
                }
            }

            ImGui::Spacing();

            drawTerrainSizeSettings(terrainMgr);
        }
        else
        {
            ImGui::TextDisabled("Select the scene or a terrain tile to edit its terrain.");
        }
        ImGui::End();
        return;
    }

    // --- Terrain Tile Settings ---
    ImGui::SeparatorText("Terrain Tile Settings");

    const bool hasTerrainTile = (terrainMgr->getTotalCount() > 0);

    // Add default tile at origin. A scene holds at most one terrain, so the
    // button only appears while the scene has none.
    if (!hasTerrainTile)
    {
        if (ImGui::Button("Add Terrain to Scene", ImVec2(-1, 0)))
        {
            addTerrainTile(0, 0);
            // Select the new tile so the neighbour controls have a reference.
            if (kTerrain *t = terrainMgr->getTile(0, 0))
                if (kMesh *m = t->getMesh())
                    manager->selectObject(m->getUuid(), true);
        }
    }

    ImGui::Spacing();

    // The selected terrain tile is the reference for the sections below.
    kTerrain *selectedTerrain = getSelectedTerrainTile(terrainMgr);

    if (selectedTerrain)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                           "Selected tile: (%d, %d)",
                           selectedTerrain->getGridX(), selectedTerrain->getGridZ());
    else
        ImGui::TextDisabled("Select a terrain tile to add or remove its neighbours.");

    ImGui::Spacing();

    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputFloat("Tile Height", &m_defaultHeight);

    // Push the height onto the selected tile and persist it. Disabled until a tile
    // is selected so the value cannot be applied to nothing.
    ImGui::BeginDisabled(selectedTerrain == nullptr);
    if (ImGui::Button("Apply##tileHeight", ImVec2(160.0f, 0.0f)))
        applyTileHeight(selectedTerrain);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Set the selected tile to this height and save it.");

    ImGui::Spacing();

    // --- Terrain size / resolution ---
    drawTerrainSizeSettings(terrainMgr);

    // The four neighbours of the selected tile, addressed by axis offset.
    const int dirDx[4] = {1, -1, 0, 0};
    const int dirDz[4] = {0, 0, 1, -1};
    const char *dirLabels[4] = {"+X", "-X", "+Z", "-Z"};

    const int selX = selectedTerrain ? selectedTerrain->getGridX() : 0;
    const int selZ = selectedTerrain ? selectedTerrain->getGridZ() : 0;

    ImGui::BeginDisabled(selectedTerrain == nullptr);

    ImGui::TextUnformatted("Add neighbour:");
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        kString id = kString(dirLabels[i]) + "##addNeighbour";
        if (ImGui::Button(id.c_str(), ImVec2(60, 28)))
        {
            const int tx = selX + dirDx[i];
            const int tz = selZ + dirDz[i];
            if (terrainMgr->getTile(tx, tz))
            {
                m_messageBoxText = "A tile already exists at (" +
                                   std::to_string(tx) + ", " + std::to_string(tz) + ").";
                m_messageBoxPending = true;
            }
            else
            {
                addTerrainTile(tx, tz);
            }
        }
    }

    ImGui::TextUnformatted("Remove neighbour:");
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        kString id = kString(dirLabels[i]) + "##removeNeighbour";
        if (ImGui::Button(id.c_str(), ImVec2(60, 28)))
        {
            const int tx = selX + dirDx[i];
            const int tz = selZ + dirDz[i];
            if (!terrainMgr->getTile(tx, tz))
            {
                m_messageBoxText = "No tile exists at (" +
                                   std::to_string(tx) + ", " + std::to_string(tz) + ").";
                m_messageBoxPending = true;
            }
            else
            {
                removeTerrainTile(tx, tz);
            }
        }
    }

    ImGui::EndDisabled();

    ImGui::Spacing();

    // --- Edit Tile -----------------------------------------------------------
    // Sculpting, painting and the height modifier stack are three views of the
    // same tile, so they share one section, one tab each.
    ImGui::SeparatorText("Edit Tile");

    if (ImGui::BeginTabBar("##terrainEditTabs"))
    {
        if (ImGui::BeginTabItem("Sculpting"))
        {
            // Toggling sculpt must clear the paint modes exactly once. Doing this
            // unconditionally each frame is what used to un-check this box as soon
            // as the user switched to another Edit Tile tab.
            {
                bool sculptModeActive = sculpt.active;
                if (ImGui::Checkbox("Sculpt Mode Active", &sculptModeActive))
                {
                    sculpt.active = sculptModeActive;
                    if (sculptModeActive)
                    {
                        paint.active = false;
                        paint.heightActive = false;
                    }
                }
            }
    if (sculpt.active)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                           "Hold left mouse in viewport to sculpt");

    // Brush mode radio buttons
    const char *modes[] = {"Raise", "Lower", "Flatten", "Smooth"};
    int currentMode = static_cast<int>(sculpt.mode);
    if (ImGui::Combo("Brush Mode", &currentMode, modes, 4))
    {
        sculpt.mode = static_cast<kTerrain::BrushMode>(currentMode);
        sculpt.affectedTile = nullptr; // Reset flatten sample on mode change
    }

    ImGui::SliderFloat("Brush Radius", &sculpt.radius, 0.5f, 50.0f, "%.1f");
    ImGui::SliderFloat("Brush Strength", &sculpt.strength, 0.01f, 1.0f, "%.2f");

    if (sculpt.mode == kTerrain::BrushMode::Flatten)
    {
        ImGui::Text("Flatten target: %.2f (sampled on first click)", sculpt.flattenHeight);
    }

    ImGui::Spacing();

            ImGui::Spacing();

            bool canClear = (selectedTerrain != nullptr);
            ImGui::BeginDisabled(!canClear);
            if (ImGui::Button("Clear Height Map", ImVec2(-1, 0)))
                m_clearHeightPending = true;
            ImGui::EndDisabled();

            if (!canClear)
                ImGui::TextDisabled("Select a terrain tile to clear its height map.");

            // Rebuild the tile's render mesh from its current height / weight data.
            ImGui::BeginDisabled(!canClear);
            if (ImGui::Button("Rebuild Mesh", ImVec2(-1, 0)))
                rebuildTileMesh(selectedTerrain);
            ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Painting"))
        {
            ImGui::Spacing();

            // Splat (mask) painting: arms the brush, which writes the weight of the
            // layer selected right below.
            {
                bool splatModeActive = paint.active;
                if (ImGui::Checkbox("Splat Paint Mode Active##splat", &splatModeActive))
                {
                    paint.active = splatModeActive;
                    if (splatModeActive)
                    {
                        sculpt.active = false;
                        paint.heightActive = false;
                        if (paint.channel < 0)
                            paint.channel = 0;
                    }
                }
            }

            // Paint target: the material layer whose painted weight the brush
            // writes. 0-based, matching the Material Layers list, and clamped so the
            // brush always starts on a valid layer.
            if (paint.channel < 0)
                paint.channel = 0;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderInt("Layer##paint", &paint.channel,
                             0, kTerrain::kMaxTerrainLayers - 1, "Layer %d");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Material layer whose weight this brush paints.");

            // Bulk weight edits for the selected layer.
            ImGui::BeginDisabled(selectedTerrain == nullptr);
            // Fill paints the layer in everywhere (white = full weight); Clear wipes
            // it back out (black = no weight). These two were previously inverted.
            if (ImGui::Button("Fill Layer##paint", ImVec2(-1, 0)))
                fillLayerWeight(selectedTerrain, paint.channel, 255);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fill this layer's weight to white (255): the layer covers the tile.");
            if (ImGui::Button("Clear Layer##paint", ImVec2(-1, 0)))
                fillLayerWeight(selectedTerrain, paint.channel, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Clear this layer's weight to black (0): the layer disappears.");
            ImGui::EndDisabled();

    if (paint.active)
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click paints this layer's weight, R-click erases it.");
        // The mask brush shares the sculpt radius; expose its own strength / fade
        // so a stroke is actually visible (both used to be fixed at the defaults).
        ImGui::SliderFloat("Paint Radius", &sculpt.radius, 0.5f, 50.0f, "%.1f");
        ImGui::SliderFloat("Paint Strength", &paint.strength, 0.01f, 1.0f, "%.2f");
        ImGui::SliderFloat("Paint Fade", &paint.fade, 0.0f, 1.0f, "%.2f");
        // Mask painting only changes the rendered result once the layers use
        // different textures; with none assigned every layer renders neutral white.
        ImGui::TextDisabled("Assign per-layer textures above to see mask changes.");
    }

    // Shared live status for every brush tool. A stroke only lands when the World
    // panel is in the Game World view AND the cursor ray is over a terrain tile.
    if (sculpt.active || paint.active)
    {
        if (paintCursorValid)
            ImGui::Text("Cursor over terrain at (%.1f, %.1f, %.1f)",
                        paintCursorPos.x, paintCursorPos.y, paintCursorPos.z);
        else
            ImGui::TextDisabled("Cursor not over terrain - hover the tile in the viewport.");
    }

    ImGui::Spacing();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Effect Layers"))
        {
    static const char *noiseTypes[] = {"fBm (smooth)", "Ridged (mountains)", "Billowed (clouds)"};

    // Effect layers always target the selected tile, so there is no separate tile
    // selector to keep in sync here.
    kTerrain *noiseTile = selectedTerrain;
    if (!noiseTile)
        ImGui::TextDisabled("Select a terrain tile to edit its effect layers.");

    ImGui::BeginDisabled(noiseTile == nullptr);

    // The Add buttons create an EMPTY layer: it contributes nothing until the user
    // expands it, sets the parameters and presses Apply.
    auto addLayer = [&](const kTerrainHeightLayer &desc)
    {
        applyHeightLayer(noiseTile, -1, desc, nullptr);
    };

    if (ImGui::Button("Add Noise Layer", ImVec2(-1, 0)))
    {
        kTerrainHeightLayer desc;
        desc.name = "Noise";
        desc.type = kTerrainHeightLayerType::Noise;
        desc.noiseScale = m_noiseScale;
        desc.noiseAmplitude = m_noiseAmplitude;
        desc.noiseOctaves = m_noiseOctaves;
        desc.noiseSeed = m_noiseSeed;
        desc.noiseType = m_noiseType;
        desc.noiseAdditive = m_noiseAdditive;
        addLayer(desc);
    }

    if (ImGui::Button("Add Hydraulic Erosion Layer", ImVec2(-1, 0)))
    {
        kTerrainHeightLayer desc;
        desc.name = "Hydraulic Erosion";
        desc.type = kTerrainHeightLayerType::HydraulicErosion;
        desc.droplets = m_erodeDroplets;
        desc.erodeRadius = m_erodeRadius;
        desc.erodeInertia = m_erodeInertia;
        desc.erodeCapacity = m_erodeCapacity;
        desc.erodeDeposit = m_erodeDeposit;
        desc.erodeErode = m_erodeErode;
        desc.erodeEvaporate = m_erodeEvaporate;
        desc.erodeGravity = m_erodeGravity;
        desc.noiseSeed = m_noiseSeed;
        addLayer(desc);
    }

    if (ImGui::Button("Add Thermal Erosion Layer", ImVec2(-1, 0)))
    {
        kTerrainHeightLayer desc;
        desc.name = "Thermal Erosion";
        desc.type = kTerrainHeightLayerType::ThermalErosion;
        desc.iterations = m_thermalIterations;
        desc.talus = m_thermalTalus;
        desc.strength = m_thermalStrength;
        addLayer(desc);
    }

    if (ImGui::Button("Add Wind Erosion Layer", ImVec2(-1, 0)))
    {
        kTerrainHeightLayer desc;
        desc.name = "Wind Erosion";
        desc.type = kTerrainHeightLayerType::WindErosion;
        desc.windDirection = m_windDir * 3.14159265f / 180.0f;
        desc.strength = m_windStrength;
        desc.iterations = 8;
        desc.talus = m_thermalTalus;
        addLayer(desc);
    }

    if (ImGui::Button("Add River Carving Layer", ImVec2(-1, 0)))
    {
        kTerrainHeightLayer desc;
        desc.name = "Rivers";
        desc.type = kTerrainHeightLayerType::Hydrology;
        desc.iterations = m_hydroIterations;
        desc.hydroK = m_hydroK;
        desc.hydroM = m_hydroM;
        desc.hydroN = m_hydroN;
        desc.hydroDeposit = m_hydroDeposit;
        addLayer(desc);
    }

    ImGui::EndDisabled();

    // --- Layer stack: enable / weight / regenerate / delete ---
    if (noiseTile)
    {
        std::vector<kTerrainHeightLayer> &layers = noiseTile->getHeightLayers();
        static const char *kLayerTypeNames[] = {
            "Noise", "Hydraulic Erosion", "Thermal Erosion", "Wind Erosion", "River Carving"};

        ImGui::Spacing();
        ImGui::Text("Layers: %zu", layers.size());

        int removeIndex = -1;
        for (int li = 0; li < static_cast<int>(layers.size()); ++li)
        {
            ImGui::PushID(li);

            kTerrainHeightLayer &layer = layers[static_cast<size_t>(li)];

            // Every setting lives inside the expansion, and nothing is committed
            // until Apply is pressed — so a slider drag never re-runs a generator.
            // The edits are parked in a persistent buffer (not a short-lived stack
            // copy) so the widgets keep the value the user typed instead of
            // reverting it on the next frame.
            kTerrainHeightLayer &desc = layerEditBufferFor(noiseTile, li, layer).desc;
            if (ImGui::TreeNode(desc.name.c_str()))
            {
                ImGui::TextDisabled("Type: %s",
                                    kLayerTypeNames[static_cast<int>(desc.type)]);

                ImGui::Checkbox("Enabled", &desc.enabled);
                ImGui::SliderFloat("Weight", &desc.weight, 0.0f, 4.0f, "%.2f");

                switch (desc.type)
                {
                case kTerrainHeightLayerType::Noise:
                    ImGui::SliderFloat("Scale", &desc.noiseScale, 1.0f, 200.0f, "%.0f");
                    ImGui::SliderFloat("Amplitude", &desc.noiseAmplitude, 0.1f, 100.0f, "%.1f");
                    ImGui::SliderInt("Octaves", &desc.noiseOctaves, 1, 8);
                    ImGui::Combo("Fractal", &desc.noiseType, noiseTypes, IM_ARRAYSIZE(noiseTypes));
                    ImGui::InputInt("Seed", &desc.noiseSeed);
                    ImGui::Checkbox("Additive", &desc.noiseAdditive);
                    break;

                case kTerrainHeightLayerType::HydraulicErosion:
                    ImGui::SliderInt("Droplets", &desc.droplets, 1000, 500000, "%d",
                                     ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Radius", &desc.erodeRadius, 1.0f, 16.0f, "%.1f");
                    ImGui::SliderFloat("Inertia", &desc.erodeInertia, 0.0f, 0.99f, "%.2f");
                    ImGui::SliderFloat("Capacity", &desc.erodeCapacity, 1.0f, 16.0f, "%.1f");
                    ImGui::SliderFloat("Deposit", &desc.erodeDeposit, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Erode", &desc.erodeErode, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Evaporate", &desc.erodeEvaporate, 0.0f, 0.1f, "%.3f");
                    ImGui::SliderFloat("Gravity", &desc.erodeGravity, 0.1f, 20.0f, "%.1f");
                    ImGui::InputInt("Seed", &desc.noiseSeed);
                    break;

                case kTerrainHeightLayerType::ThermalErosion:
                    ImGui::SliderInt("Iterations", &desc.iterations, 1, 64);
                    ImGui::SliderFloat("Talus", &desc.talus, 0.0f, 5.0f, "%.2f");
                    ImGui::SliderFloat("Strength", &desc.strength, 0.0f, 1.0f, "%.2f");
                    break;

                case kTerrainHeightLayerType::WindErosion:
                {
                    float degrees = desc.windDirection * 180.0f / 3.14159265f;
                    ImGui::SliderFloat("Direction (deg)", &degrees, 0.0f, 360.0f, "%.0f");
                    desc.windDirection = degrees * 3.14159265f / 180.0f;
                    ImGui::SliderFloat("Strength", &desc.strength, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderInt("Iterations", &desc.iterations, 1, 32);
                    ImGui::SliderFloat("Talus", &desc.talus, 0.0f, 5.0f, "%.2f");
                    break;
                }

                case kTerrainHeightLayerType::Hydrology:
                    ImGui::SliderInt("Iterations", &desc.iterations, 1, 32);
                    ImGui::SliderFloat("K", &desc.hydroK, 0.001f, 0.2f, "%.3f");
                    ImGui::SliderFloat("M", &desc.hydroM, 0.1f, 1.0f, "%.2f");
                    ImGui::SliderFloat("N", &desc.hydroN, 0.5f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Deposit", &desc.hydroDeposit, 0.0f, 0.9f, "%.2f");
                    break;
                }

                if (ImGui::Button("Apply"))
                    applyHeightLayer(noiseTile, li, desc,
                                     makeHeightLayerGenerator(noiseTile, desc));
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                    removeIndex = li;

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (removeIndex >= 0)
            removeHeightLayerUndoable(noiseTile, removeIndex);
    }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Confirmation for "Clear Height Map". Drawn after the tab bar so it is always
    // available whichever tab is showing.
    if (m_clearHeightPending)
    {
        ImGui::OpenPopup("Clear Height Map##confirm");
        m_clearHeightPending = false;
    }

    if (ImGui::BeginPopupModal("Clear Height Map##confirm", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Clear this tile's height map?");
        ImGui::Separator();
        ImGui::Text("Target height: %.2f", m_defaultHeight);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
        ImGui::TextWrapped("The sculpted height returns to the default height and every "
                           "effect layer's contribution is cleared, so the tile reads flat "
                           "again.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (ImGui::Button("Clear", ImVec2(120, 0)))
        {
            clearSelectedTileHeight(getSelectedTerrainTile(terrainMgr));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // Loading is fully automatic and has no user interface — the editor keeps
    // every tile's mesh resident and never unloads tiles.
    ensureTilesLoaded();

    ImGui::Spacing();

    // Terrain assets (.height / .splat / .hlayers / .mat) are created, updated and
    // deleted with each tile under Library/Terrains, so there is no manual
    // save / load step to expose here.

    ImGui::Spacing();

    // --- Layer Configuration ---
    ImGui::SeparatorText("Material Layers");

    // Follow the editor selection; the manual grid coordinates stay as a fallback
    // for when nothing is selected. Both are function-scope because the tile
    // coordinates are also printed further down.
    static int selectedTileGridX = 0;
    static int selectedTileGridZ = 0;

    kTerrain *selectedTile = getSelectedTerrainTile(terrainMgr);
    if (!selectedTile)
    {
        ImGui::InputInt("Tile Grid X##layer", &selectedTileGridX);
        ImGui::InputInt("Tile Grid Z##layer", &selectedTileGridZ);
        selectedTile = terrainMgr->getTile(selectedTileGridX, selectedTileGridZ);
    }
    if (selectedTile)
    {
        auto &layers = selectedTile->getLayers();
        // The tile packs sixteen per-layer weights into four RGBA weight maps, so
        // the material blends up to sixteen layers (TerrainPBR's array path).
        const size_t maxMaterialLayers = kTerrain::kMaxTerrainLayers;
        ImGui::Text("Layers: %zu/%zu", layers.size(), maxMaterialLayers);

        // Deferred: removing a layer mutates `layers`, which must not happen while
        // iterating it (or while the tree node using it is still open).
        int removeLayerRequest = -1;

        for (size_t i = 0; i < layers.size(); ++i)
        {
            auto &layer = layers[i];
            kString label = layer.name + "##layer" + std::to_string(i);
            if (ImGui::TreeNode(label.c_str()))
            {
                static char nameBuf[128];
                strncpy_s(nameBuf, sizeof(nameBuf), layer.name.c_str(), _TRUNCATE);
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                    layer.name = nameBuf;
                // Per-layer texture tiling: repeats of this layer's texture across
                // the tile (uploaded as u_Tiling[this layer]).
                ImGui::SetNextItemWidth(230.0f);
                if (ImGui::SliderFloat("Tiling##layer", &layer.tiling, 1.0f, 128.0f, "%.1f"))
                {
                    selectedTile->syncLayerTiling();
                    selectedTile->saveOwnedAssets();
                    manager->projectSaved = false;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How many times this layer's texture repeats across the tile.");

                // (Min/Max Height and Slope Min/Max were removed: the shader does
                // not consume them, so editing them had no effect.)

                // Per-layer textures packed into the terrain's sampler2DArrays.
                // Per-layer textures. These are the *only* terrain textures the
                // user picks: they are packed into the terrain's sampler2DArrays
                // and never replace the tile's own material.
                auto texField = [&](const char *label, kString &path, int slot)
                {
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(90.0f);

                    kString shown = path.empty() ? kString("(browse...)")
                                                : fs::path(path).filename().string();
                    kString id = shown + "##tex" + std::to_string(i) + label;
                    if (ImGui::Button(id.c_str(), ImVec2(230, 0)))
                    {
                        m_texPickTile = selectedTile;
                        m_texPickLayer = static_cast<int>(i);
                        m_texPickSlot = slot;
                        // Start from a clean selection/search each time.
                        m_texPickSelected.clear();
                        m_texPickSearch[0] = '\0';
                        m_texPickOpen = true;
                    }

                    if (ImGui::IsItemHovered() && !path.empty())
                        ImGui::SetTooltip("%s", path.c_str());

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload =
                                ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
                        {
                            kString assetUuid((const char *)payload->Data);
                            auto nl = assetUuid.find('\n');
                            if (nl != kString::npos)
                                assetUuid = assetUuid.substr(0, nl);

                            auto it = manager->fileMap.find(assetUuid);
                            // Asset types come from the project scanner, which
                            // labels images "image" (the picker below already
                            // looks for that); "texture" matched nothing, so a
                            // dropped texture was silently ignored.
                            if (it != manager->fileMap.end() &&
                                it->second.type == "image")
                            {
                                path = (manager->projectPath / "Assets" /
                                        it->second.path).string();
                                selectedTile->rebuildLayerArrays();
                                rebuildTileMesh(selectedTile);
                                selectedTile->saveOwnedAssets();
                                manager->projectSaved = false;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                };
                texField("Albedo", layer.albedoPath, 0);
                texField("Normal", layer.normalPath, 1);
                texField("ORM", layer.ormPath, 2);

                // Which layer's painted weight masks this layer. -1 ("auto") uses
                // the layer's own weight, which is exactly what painting this layer
                // edits, so the default needs no extra choice.
                {
                    bool autoMask = (layer.maskChannel < 0);
                    if (ImGui::Checkbox("Auto Mask##terrainLayer", &autoMask))
                    {
                        layer.maskChannel = autoMask ? -1 : static_cast<int>(i);
                        selectedTile->syncLayerMaskChannels();
                        selectedTile->saveOwnedAssets();
                        manager->projectSaved = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Auto: the layer is masked by its own painted weight.");

                    if (!autoMask)
                    {
                        int maskLayer = layer.maskChannel;
                        ImGui::SetNextItemWidth(230.0f);
                        if (ImGui::SliderInt("Mask Layer", &maskLayer, 0,
                                             kTerrain::kMaxTerrainLayers - 1))
                        {
                            layer.maskChannel = maskLayer;
                            selectedTile->syncLayerMaskChannels();
                            selectedTile->saveOwnedAssets();
                            manager->projectSaved = false;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Painted weight of this layer masks the layer.");
                    }
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.16f, 0.16f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
                if (ImGui::Button("Remove layer##terrainLayer", ImVec2(230.0f, 0.0f)))
                    removeLayerRequest = static_cast<int>(i);
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Delete this material layer (weights, textures and mask refs are shifted).");

                ImGui::TreePop();
            }
        }

        if (removeLayerRequest >= 0 && removeLayerRequest < static_cast<int>(layers.size()))
            removeTerrainLayer(selectedTile, removeLayerRequest);

        ImGui::BeginDisabled(layers.size() >= maxMaterialLayers);
        if (ImGui::Button("Add Layer"))
        {
            kTerrainLayer newLayer;
            // setLayer() updates the layer that shares the new layer's name, so a
            // constant default name made every Add after the first overwrite the
            // previous layer (the stack never grew past one).
            int n = static_cast<int>(layers.size());
            newLayer.name = "Layer " + std::to_string(n);
            while (std::any_of(layers.begin(), layers.end(),
                               [&](const kTerrainLayer &l) { return l.name == newLayer.name; }))
                newLayer.name = "Layer " + std::to_string(++n);

            selectedTile->setLayer(newLayer);
            // The new layer needs its weight channel uploaded and its slot in the
            // .mat / .hlayers payloads written, even though it has no texture yet.
            selectedTile->syncLayerMaskChannels();
            selectedTile->saveOwnedAssets();
            manager->projectSaved = false;
        }
        ImGui::EndDisabled();
        if (layers.size() >= maxMaterialLayers)
            ImGui::TextDisabled("Maximum of %zu layers.", maxMaterialLayers);

        // --- Terrain material (auto-managed) ---
        // Each tile creates its own TerrainPBR material and deletes it with the
        // tile. It is deliberately not re-assignable from the Inspector or by
        // dragging a .mat onto the tile — only its parameters are edited here.
        ImGui::Spacing();
        ImGui::SeparatorText("Terrain Material Settings");

        if (kMaterial *terrainMat = selectedTile->getTerrainMaterial())
        {
            // Edits are staged and only committed (and written to the tile's .mat)
            // when Apply is pressed, so a slider drag never touches the asset.
            if (m_matSettingsTile != selectedTile)
            {
                m_matSettingsTile = selectedTile;
                const auto &params = terrainMat->getParams();
                auto read = [&](const char *name, float fallback)
                {
                    auto it = params.find(kString(name));
                    return (it != params.end()) ? it->second.value.x : fallback;
                };
                m_matSettings.blendSharpness     = read("u_BlendSharpness", 1.0f);
                m_matSettings.stochastic         = read("u_Stochastic", 0.0f) > 0.5f;
                m_matSettings.triplanar          = read("u_Triplanar", 0.0f) > 0.5f;
                m_matSettings.rotateUV           = read("u_RotateUV", 0.0f) > 0.5f;
                m_matSettings.uvRotation         = read("u_UVRotation", 0.0f) * 57.29578f;
                m_matSettings.triplanarStrength  = read("u_TriplanarStrength", 0.75f);
                m_matSettings.triplanarSharpness = read("u_TriplanarSharpness", 3.0f);
                m_matSettings.noiseVariegation   = read("u_NoiseVariegation", 0.25f);
                m_matSettings.noiseScale         = read("u_NoiseScale", 0.02f);
            }

            // Tiling is edited per material layer (see each layer's tree node above).
            // Every control applies immediately — there is no Apply button. The
            // .mat is only written to disk once the interaction ends, so dragging a
            // slider updates the view live without rewriting the asset every frame.
            bool matChanged = false;

            matChanged |= ImGui::SliderFloat("Blend Sharpness", &m_matSettings.blendSharpness,
                                             0.1f, 8.0f, "%.2f");

            // Stochastic anti-tiling: random per-cell rotation that hides repeats.
            matChanged |= ImGui::Checkbox("Stochastic Anti-Tiling", &m_matSettings.stochastic);

            // Global UV rotation — rotates the world-space UVs so the repeat grid is
            // no longer axis-aligned (breaks the visible tiling seam direction).
            matChanged |= ImGui::Checkbox("Rotate UV", &m_matSettings.rotateUV);
            ImGui::BeginDisabled(!m_matSettings.rotateUV);
            matChanged |= ImGui::SliderFloat("UV Rotation", &m_matSettings.uvRotation,
                                             0.0f, 360.0f, "%.1f deg");
            ImGui::EndDisabled();

            // Triplanar projection is opt-in; the strength / sharpness sliders only
            // matter while it is enabled.
            matChanged |= ImGui::Checkbox("Triplanar", &m_matSettings.triplanar);
            ImGui::BeginDisabled(!m_matSettings.triplanar);
            matChanged |= ImGui::SliderFloat("Triplanar Strength",
                                             &m_matSettings.triplanarStrength, 0.0f, 1.0f, "%.2f");
            matChanged |= ImGui::SliderFloat("Triplanar Sharpness",
                                             &m_matSettings.triplanarSharpness, 0.5f, 16.0f, "%.1f");
            ImGui::EndDisabled();

            // Procedural noise variegation, with its own on/off above the amount and
            // scale it controls.
            matChanged |= ImGui::Checkbox("Noise", &m_matSettings.noiseEnabled);
            ImGui::BeginDisabled(!m_matSettings.noiseEnabled);
            matChanged |= ImGui::SliderFloat("Noise Variegation",
                                             &m_matSettings.noiseVariegation, 0.01f, 1.0f, "%.2f");
            matChanged |= ImGui::SliderFloat("Noise Scale",
                                             &m_matSettings.noiseScale, 0.001f, 0.5f, "%.3f");
            ImGui::EndDisabled();

            if (matChanged)
            {
                terrainMat->setParamFloat("u_BlendSharpness", m_matSettings.blendSharpness);
                terrainMat->setParamFloat("u_Stochastic", m_matSettings.stochastic ? 1.0f : 0.0f);
                terrainMat->setParamFloat("u_Triplanar", m_matSettings.triplanar ? 1.0f : 0.0f);
                terrainMat->setParamFloat("u_RotateUV", m_matSettings.rotateUV ? 1.0f : 0.0f);
                terrainMat->setParamFloat("u_UVRotation",
                                          m_matSettings.uvRotation * 3.14159265f / 180.0f);
                terrainMat->setParamFloat("u_TriplanarStrength", m_matSettings.triplanarStrength);
                terrainMat->setParamFloat("u_TriplanarSharpness", m_matSettings.triplanarSharpness);
                terrainMat->setParamFloat("u_NoiseVariegation",
                                          m_matSettings.noiseEnabled ? m_matSettings.noiseVariegation
                                                                     : 0.0f);
                terrainMat->setParamFloat("u_NoiseScale", m_matSettings.noiseScale);

                // Tiling lives on each material layer.
                selectedTile->syncLayerTiling();
                manager->projectSaved = false;

                // Persist once the user lets go rather than on every drag frame.
                if (!ImGui::IsAnyItemActive())
                    selectedTile->saveOwnedAssets();
            }
        }
        else
        {
            m_matSettingsTile = nullptr;
            ImGui::TextDisabled("No material yet (tile not loaded).");
        }

        // --- Terrain physics: collider -------------------------------------
        // A Jolt HeightFieldShape is the recommended collider for regular height
        // grids — far cheaper than a triangle mesh. The runtime builds the sample
        // grid from this tile's mesh vertices when the game starts.
        ImGui::Spacing();
        ImGui::SeparatorText("Terrain Physics");

        kObject *tileObject = selectedTile->getMesh()
                                  ? static_cast<kObject *>(selectedTile->getMesh())
                                  : nullptr;
        const bool hasCollider =
            tileObject && tileObject->getHasPhysicsDesc() &&
            tileObject->getPhysicsDesc().shape.type == kPhysicsShapeType::HeightField;

        // Collider resolution — samples per side of the HeightField shape. Higher
        // values give the collider more triangles (a finer, more accurate mesh).
        if (m_collisionResTile != selectedTile)
        {
            m_collisionResTile = selectedTile;
            m_collisionResolution =
                (hasCollider && tileObject->getPhysicsDesc().shape.heightSampleCount >= 2)
                    ? tileObject->getPhysicsDesc().shape.heightSampleCount
                    : 129;
        }

        ImGui::BeginDisabled(tileObject == nullptr);
        ImGui::SliderInt("Collision Resolution", &m_collisionResolution, 8, 512, "%d",
                         ImGuiSliderFlags_Logarithmic);
        // Changing the value re-creates an existing collider on release, so the
        // slider is not a dead control that only took effect through the button
        // below (and only after a tile had a collider at all).
        if (ImGui::IsItemDeactivatedAfterEdit() && hasCollider)
        {
            generateTileCollider(selectedTile, m_collisionResolution);
            showCollider = true;
        }
        ImGui::TextDisabled("Higher = more triangles, more precise collision.");
        // (Re)creates the collider from the tile's current geometry at the
        // resolution above, and turns its wireframe on so the result is visible.
        if (ImGui::Button("Generate Tile Collider", ImVec2(-1, 0)))
        {
            generateTileCollider(selectedTile, m_collisionResolution);
            showCollider = true;
        }
        ImGui::EndDisabled();

        // Only offer removal while the tile actually owns a collider.
        if (hasCollider)
        {
            if (ImGui::Button("Remove Tile Collider", ImVec2(-1, 0)))
            {
                tileObject->setHasPhysicsDesc(false);
                showCollider = false;
                manager->projectSaved = false;
            }
        }

        ImGui::BeginDisabled(!hasCollider);
        ImGui::Checkbox("Show collider", &showCollider);
        ImGui::EndDisabled();
    }
    else
    {
        ImGui::TextDisabled("No tile at (%d, %d). Create one above.", selectedTileGridX, selectedTileGridZ);
    }

    ImGui::End();

    // Layer texture picker — drawn *after* the panel window because it is a
    // top-level window (plus its input blocker) rather than a nested one.
    drawTexturePickerPopup();
}

void PanelTerrain::addTerrainTile(int gridX, int gridZ)
{
    if (!manager || !manager->terrainManager)
        return;

    kTerrainManager *mgr = manager->terrainManager;

    // Already present → nothing to do (createTile would just return it).
    if (mgr->getTile(gridX, gridZ))
        return;

    kTerrain *tile = mgr->createTile(gridX, gridZ);
    if (!tile)
        return;

    tile->fillHeight(m_defaultHeight);

    // createTile() only constructs the object — the GPU mesh is built by
    // rebuildMesh()/reload(). Build it now so the tile is visible immediately.
    tile->reload(true);

    // The tile now has its mesh (and therefore the UUID its file names derive
    // from), so create and persist the assets it owns: .height, .splat,
    // .hlayers and its own TerrainPBR .mat.
    if (manager->terrainManager->getAssetDirectory().empty())
    {
        fs::path libDir = manager->projectPath / "Library" / "Terrains";
        fs::create_directories(libDir);
        manager->terrainManager->setAssetDirectory(libDir.string());
    }
    tile->createOwnedAssets(manager->terrainManager->getAssetDirectory());
    tile->saveOwnedAssets();

    // Existing neighbours may have been stitched against a missing edge; refresh
    // them so the seams line up with the newly added tile.
    rebuildNeighborMeshes(gridX, gridZ);

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;
}

void PanelTerrain::removeTerrainTile(int gridX, int gridZ)
{
    if (!manager || !manager->terrainManager)
        return;

    kTerrainManager *mgr = manager->terrainManager;

    kTerrain *tile = mgr->getTile(gridX, gridZ);
    if (!tile)
        return;

    // Removing a tile deletes its scene mesh, so drop it from the selection
    // first — otherwise the editor would keep a dangling mesh pointer (which
    // crashes e.g. the nav-mesh debug draw that dereferences selectedObject).
    if (kMesh *mesh = tile->getMesh())
    {
        const kString uuid = mesh->getUuid();

        kObject *&activeSel = manager->getActiveSelectedObject();
        if (activeSel == mesh)
            activeSel = nullptr;
        if (manager->selectedObject == mesh)
            manager->selectedObject = nullptr;

        auto eraseUuid = [&uuid](std::vector<kString> &list)
        {
            list.erase(std::remove(list.begin(), list.end(), uuid), list.end());
        };
        eraseUuid(manager->getActiveSelectedObjects());
        eraseUuid(manager->selectedObjects);
    }

    // The tile is about to be destroyed — drop any in-progress layer edits it owns
    // so a later tile cannot accidentally reuse them.
    clearLayerEditBuffers(tile);

    mgr->removeTile(gridX, gridZ);

    // The former neighbours were stitched to this tile; rebuild them so their
    // edges close up cleanly.
    rebuildNeighborMeshes(gridX, gridZ);

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;
}

void PanelTerrain::rebuildNeighborMeshes(int gridX, int gridZ)
{
    if (!manager || !manager->terrainManager)
        return;

    kTerrainManager *mgr = manager->terrainManager;
    const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto &o : offsets)
    {
        kTerrain *n = mgr->getTile(gridX + o[0], gridZ + o[1]);
        if (n && n->isLoaded())
            rebuildTileMesh(n);
    }
}

void PanelTerrain::ensureTilesLoaded()
{
    if (!manager || !manager->terrainManager)
        return;

    // The editor keeps every tile resident: tiles are created explicitly through
    // the Tile Management controls and are never unloaded here (unloading would
    // delete the tile's scene mesh and could leave the current selection
    // dangling). This only loads any tile that is somehow missing its mesh.
    for (const auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (tile && !tile->isLoaded())
            tile->reload(true);
    }
}

kString PanelTerrain::getDataDirForTerrain(const kString &terrainFilePath)
{
    fs::path tp(terrainFilePath);
    kString stem = tp.stem().string();
    return (tp.parent_path() / (stem + "_data")).string();
}

void PanelTerrain::onTerrainFileRenamed(const kString &oldPath, const kString &newPath)
{
    kString oldDataDir = getDataDirForTerrain(oldPath);
    kString newDataDir = getDataDirForTerrain(newPath);

    if (fs::exists(oldDataDir) && fs::is_directory(oldDataDir))
    {
        try
        {
            fs::rename(oldDataDir, newDataDir);
        }
        catch (const fs::filesystem_error &e)
        {
            std::cerr << "[PanelTerrain] Failed to rename data folder: " << e.what() << std::endl;
        }
    }
}

void PanelTerrain::loadProjectTerrains()
{
    if (!manager || !manager->terrainManager)
        return;

    // Tiles are (re)loaded here, so any in-progress layer edits are stale.
    m_layerEditBuffers.clear();

    // Terrain payloads live in Library/Terrains/, named after the tile's mesh
    // UUID. The manager owns that folder, so each tile creates / reads / deletes
    // its own .height, .splat, .hlayers and .mat files without user involvement.
    fs::path libDir = manager->projectPath / "Library" / "Terrains";

    // Legacy .terrain files (if a project still has any) are picked up first.
    manager->terrainManager->loadFromDirectory(libDir.string());

    setTerrainAssetDirectory(libDir.string());

    for (const auto &pair : manager->terrainManager->getTiles())
    {
        kTerrain *tile = pair.second.get();
        if (!tile)
            continue;

        tile->loadOwnedAssets();
        if (tile->isLoaded())
            rebuildTileMesh(tile);
    }

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();

    m_terrainDirectory = libDir.string();
}

void PanelTerrain::saveProjectTerrains()
{
    if (!manager)
    {
        std::cerr << "saveProjectTerrains: manager is null\n";
        return;
    }
    if (!manager->terrainManager)
    {
        std::cerr << "saveProjectTerrains: terrainManager is null\n";
        return;
    }

    fs::path libDir = manager->projectPath / "Library" / "Terrains";
    fs::create_directories(libDir);

    // Owned payloads: each tile writes its own height / splat / layers / material.
    setTerrainAssetDirectory(libDir.string());
    saveAllOwnedAssets();

    std::cerr << "saveProjectTerrains: saved " << manager->terrainManager->getTotalCount()
              << " tiles to " << libDir << "\n";
}