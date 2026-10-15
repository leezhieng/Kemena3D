/**
 * @file panel_terrain.cpp
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#include "panel_terrain.h"
#include "util.h"
#include "mainmenu.h"   // for terrainDockId / terrainDockRestore (panel docking persistence)
#include "commands.h"   // for TerrainEditCommand

#include <imgui.h>
#include <set>
#include <random>
#include <cmath>
#include <vector>
#include <algorithm>

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

    // Search for the tile containing the world position
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

    // On first stroke in Flatten mode, sample the target height
    if (sculpt.mode == kTerrain::BrushMode::Flatten && sculpt.affectedTile != foundTile)
    {
        sculpt.flattenHeight = foundTile->sampleHeight(worldPos);
    }

    foundTile->applyBrush(worldPos, sculpt.radius, sculpt.strength,
                          sculpt.mode, sculpt.flattenHeight);

    // Fast polygon update for live visual feedback (updates vertex Y and normals on GPU)
    foundTile->updateMesh();

    sculpt.affectedTile = foundTile;
    sculpt.needsRebuild = true;
}

void PanelTerrain::rebuildSculptedTile()
{
    if (!sculpt.needsRebuild || !sculpt.affectedTile)
        return;

    // Use fast update instead of full rebuild to preserve the material.
    // updateMesh() updates vertex positions, normals, GPU buffers, and AABB
    // without destroying/recreating the mesh (which would delete the material).
    sculpt.affectedTile->updateMesh();
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

    // Use updateMesh to preserve material (same reason as sculpting)
    paint.affectedTile->updateMesh();
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

    tile->rebuildMesh(true);
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

    tile->rebuildMesh(true);
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

    tile->rebuildMesh(true);
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

    tile->rebuildMesh(true);
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

    tile->rebuildMesh(true);
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
        tile->rebuildMesh(true);

    cmd->diffs.push_back(std::move(diff));
    manager->undoRedo.push(std::move(cmd));

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;

    // Persist the layer straight away: offsets are not cheap to regenerate.
    tile->saveOwnedAssets();
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
        tile->rebuildMesh(true);

    cmd->diffs.push_back(std::move(diff));
    manager->undoRedo.push(std::move(cmd));

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;
    tile->saveOwnedAssets();
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

void PanelTerrain::clearSelectedTileHeight(kTerrain *tile)
{
    if (!tile || !manager)
        return;

    beginTerrainEdit(tile, true, false);

    // Back to the flat default height ...
    float *base = tile->getBaseHeightData();
    if (base)
        std::fill(base, base + tile->getHeightDataSize(), m_defaultHeight);

    // ... and the effect layers hold height deltas, so their contribution goes too.
    for (auto &layer : tile->getHeightLayers())
        std::fill(layer.offsets.begin(), layer.offsets.end(), 0.0f);

    tile->recomposeHeight();
    if (tile->isLoaded())
        tile->rebuildMesh(true);

    commitTerrainEdit();

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();
    manager->projectSaved = false;
    tile->saveOwnedAssets();
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

    // Add default tile at origin. Disabled once the scene already owns a tile —
    // a scene holds at most one terrain, so creating another is not allowed.
    ImGui::BeginDisabled(hasTerrainTile);
    if (ImGui::Button("Add Terrain to Scene", ImVec2(-1, 0)))
    {
        addTerrainTile(0, 0);
        // Select the new tile so the neighbour controls have a reference.
        if (kTerrain *t = terrainMgr->getTile(0, 0))
            if (kMesh *m = t->getMesh())
                manager->selectObject(m->getUuid(), true);
    }
    ImGui::EndDisabled();
    if (hasTerrainTile)
        ImGui::TextDisabled("A terrain already exists in this scene.");

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

    ImGui::InputFloat("Default Height", &m_defaultHeight);

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

            ImGui::Separator();
            ImGui::Spacing();

            bool canClear = (selectedTerrain != nullptr);
            ImGui::BeginDisabled(!canClear);
            if (ImGui::Button("Clear Height Map", ImVec2(-1, 0)))
                m_clearHeightPending = true;
            ImGui::EndDisabled();

            if (!canClear)
                ImGui::TextDisabled("Select a terrain tile to clear its height map.");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Painting"))
        {
            // Height paint is the painting "mode", so it leads the tab.
            {
                bool paintModeActive = paint.heightActive;
                if (ImGui::Checkbox("Paint Mode Active", &paintModeActive))
                {
                    paint.heightActive = paintModeActive;
                    if (paintModeActive)
                    {
                        sculpt.active = false;
                        paint.active = false;
                    }
                }
            }

            if (paint.heightActive)
                ImGui::TextDisabled("Left click lowers, right click raises the height.");

            ImGui::Spacing();
            ImGui::Text("Splat Mask (R/G/B/A):");
    static bool splatSelected[4] = {false, false, false, false};
    ImU32 splatColors[4] = {
        IM_COL32(255, 60, 60, 255),
        IM_COL32(60, 255, 60, 255),
        IM_COL32(60, 60, 255, 255),
        IM_COL32(200, 200, 200, 255),
    };
    const char *splatLabels[4] = {"R", "G", "B", "A"};

    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::ColorButton(splatLabels[i], ImVec4(((splatColors[i] >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f, ((splatColors[i] >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f, ((splatColors[i] >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f, ((splatColors[i] >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f),
                               ((paint.channel == i) ? ImGuiColorEditFlags_NoTooltip : 0) | ImGuiColorEditFlags_NoBorder,
                               ImVec2(48, 24)))
        {
            // Turn off sculpt when entering paint mode
            sculpt.active = false;
            paint.heightActive = false;
            paint.active = true;
            paint.channel = i;
        }
    }

    // Paint target layer (0..15). Layers 0..3 also have the quick R/G/B/A swatches.
    ImGui::SliderInt("Layer##paint", &paint.channel, 0, kTerrain::kMaxTerrainLayers - 1);

    if (paint.active)
    {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click paint, R-click erase mask channel.");
        // Mask painting only changes the rendered result once the layers use
        // different textures; with none assigned every layer renders neutral white.
        ImGui::TextDisabled("Assign per-layer textures above to see mask changes.");
    }
    if (paint.heightActive)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click lower, R-click raise height");

    // Shared live status for every brush tool. A stroke only lands when the World
    // panel is in the Game World view AND the cursor ray is over a terrain tile.
    if (sculpt.active || paint.active || paint.heightActive)
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
            ImGui::TextDisabled("Height modifier layers, combined on top of the sculpted height.");

    static const char *noiseTypes[] = {"fBm (smooth)", "Ridged (mountains)", "Billowed (clouds)"};
    ImGui::TextDisabled("Add a layer, then expand it to set its parameters.");

    // Layer target tile — defaults to the selected tile so the common case needs
    // no manual coordinates.
    static int layerGridX = 0;
    static int layerGridZ = 0;
    if (selectedTerrain)
    {
        layerGridX = selectedTerrain->getGridX();
        layerGridZ = selectedTerrain->getGridZ();
    }
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile X##layer", &layerGridX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile Z##layer", &layerGridZ);

    kTerrain *noiseTile = terrainMgr->getTile(layerGridX, layerGridZ);
    if (!noiseTile)
        ImGui::TextDisabled("No tile at (%d, %d).", layerGridX, layerGridZ);

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
            kTerrainHeightLayer desc = layer;
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
        ImGui::Text("Layers for tile (%d, %d): %zu", selectedTileGridX, selectedTileGridZ, layers.size());

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
                ImGui::InputFloat("Tile Size", &layer.tileSize);
                ImGui::InputFloat("Min Height", &layer.minHeight);
                ImGui::InputFloat("Max Height", &layer.maxHeight);
                ImGui::InputFloat("Slope Min", &layer.slopeMin);
                ImGui::InputFloat("Slope Max", &layer.slopeMax);

                // Per-layer textures packed into the terrain's sampler2DArrays.
                // Per-layer textures. These are the *only* terrain textures the
                // user picks: they are packed into the terrain's sampler2DArrays
                // and never replace the tile's own material.
                auto texField = [&](const char *label, kString &path)
                {
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(90.0f);

                    kString shown = path.empty() ? kString("(drop a texture)")
                                                : fs::path(path).filename().string();
                    kString id = shown + "##tex" + std::to_string(i) + label;
                    ImGui::Button(id.c_str(), ImVec2(230, 0));

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
                            if (it != manager->fileMap.end() &&
                                it->second.type == "texture")
                            {
                                path = (manager->projectPath / "Assets" /
                                        it->second.path).string();
                                selectedTile->rebuildLayerArrays();
                                selectedTile->rebuildMesh(true);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                };
                texField("Albedo", layer.albedoPath);
                texField("Normal", layer.normalPath);
                texField("ORM", layer.ormPath);
                texField("Mask", layer.maskPath);

                ImGui::TreePop();
            }
        }

        if (ImGui::Button("Add Layer"))
        {
            kTerrainLayer newLayer;
            newLayer.name = "New Layer";
            selectedTile->setLayer(newLayer);
        }

        ImGui::SameLine();
        if (ImGui::Button("Rebuild Mesh"))
        {
            selectedTile->rebuildMesh(true);
        }

        if (ImGui::Button("Rebuild Layer Textures"))
        {
            selectedTile->rebuildLayerArrays();
            selectedTile->rebuildMesh(true);
        }

        // --- Terrain material (auto-managed) ---
        // Each tile creates its own TerrainPBR material and deletes it with the
        // tile. It is deliberately not re-assignable from the Inspector or by
        // dragging a .mat onto the tile — only its parameters are edited here.
        ImGui::Spacing();
        ImGui::SeparatorText("Terrain Material (auto-managed)");

        if (kMaterial *terrainMat = selectedTile->getTerrainMaterial())
        {
            auto floatParam = [&](const char *label, const char *name,
                                  float fallback, float lo, float hi, const char *fmt)
            {
                const auto &params = terrainMat->getParams();
                auto it = params.find(kString(name));
                float value = (it != params.end()) ? it->second.value.x : fallback;
                if (ImGui::SliderFloat(label, &value, lo, hi, fmt))
                {
                    terrainMat->setParamFloat(name, value);
                    selectedTile->saveOwnedAssets();
                }
            };

            float tiling = 16.0f;
            {
                const auto &params = terrainMat->getParams();
                auto it = params.find(kString("u_Tiling[0]"));
                if (it != params.end())
                    tiling = it->second.value.x;
            }
            if (ImGui::SliderFloat("Tiling", &tiling, 1.0f, 128.0f, "%.1f"))
            {
                for (int li = 0; li < 4; ++li)
                    terrainMat->setParamFloat("u_Tiling[" + std::to_string(li) + "]", tiling);
                selectedTile->saveOwnedAssets();
            }

            floatParam("Blend Sharpness", "u_BlendSharpness", 1.0f, 0.1f, 8.0f, "%.2f");
            floatParam("Stochastic", "u_Stochastic", 1.0f, 0.0f, 1.0f, "%.0f");
            floatParam("Triplanar Strength", "u_TriplanarStrength", 0.75f, 0.0f, 1.0f, "%.2f");
            floatParam("Triplanar Sharpness", "u_TriplanarSharpness", 3.0f, 0.5f, 16.0f, "%.1f");
            floatParam("Noise Variegation", "u_NoiseVariegation", 0.25f, 0.0f, 1.0f, "%.2f");
            floatParam("Noise Scale", "u_NoiseScale", 0.02f, 0.001f, 0.5f, "%.3f");

            ImGui::TextDisabled("Material .mat is written to Library/Terrains with the tile.");
        }
        else
        {
            ImGui::TextDisabled("No material yet (tile not loaded).");
        }

        // Generate the terrain collision shape. A Jolt HeightFieldShape is the
        // recommended collider for regular height grids — far cheaper than a
        // triangle mesh. The runtime builds the sample grid from this tile's
        // mesh vertices when the game starts.
        if (ImGui::Button("Generate HeightField Collider", ImVec2(-1, 0)) && selectedTile->getMesh())
        {
            kObject *obj = static_cast<kObject *>(selectedTile->getMesh());
            kPhysicsObjectDesc &desc = obj->getPhysicsDesc();
            desc.shape.type = kPhysicsShapeType::HeightField;
            desc.type = kPhysicsObjectType::Static;
            desc.layer = "Default";
            obj->setHasPhysicsDesc(true);
        }
    }
    else
    {
        ImGui::TextDisabled("No tile at (%d, %d). Create one above.", selectedTileGridX, selectedTileGridZ);
    }

    ImGui::End();
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
            n->rebuildMesh(true);
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
            tile->rebuildMesh(true);
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