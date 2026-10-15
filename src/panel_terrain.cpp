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
        const float *data = tile->getHeightData();
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

        // --- Height field diff ---
        if (s.height && s.heightBefore.size() == static_cast<size_t>(res) * res)
        {
            const float *after = tile->getHeightData();
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
    float *data = foundTile->getHeightData();

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
            falloff *= effectiveStrength;

            int idx = z * res + x;
            float oldVal = data[idx];
            data[idx] = oldVal + falloff;
        }
    }

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
    bool terrainSelected = false;
    if (kObject *sel = manager->getActiveSelectedObject())
    {
        kMesh *sm = dynamic_cast<kMesh *>(sel);
        if (sm && sm->getSerializeType() == "terrain")
            terrainSelected = true;
        else if (sel->getType() == kNodeType::NODE_TYPE_TERRAIN)
            terrainSelected = true;
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
            ImGui::TextDisabled("This scene has no terrain yet.");
            ImGui::Spacing();
            if (ImGui::Button("Create New Terrain", ImVec2(-1, 0)))
                manager->createTerrain();
        }
        else
        {
            ImGui::TextDisabled("Select the scene or a terrain tile to edit its terrain.");
        }
        ImGui::End();
        return;
    }

    // --- Tile Management ---
    ImGui::SeparatorText("Tile Management");

    const bool hasTerrainTile = (terrainMgr->getTotalCount() > 0);

    // Add default tile at origin. Disabled once the scene already owns a tile —
    // a scene holds at most one terrain, so creating another is not allowed.
    ImGui::BeginDisabled(hasTerrainTile);
    if (ImGui::Button("Add Default Terrain", ImVec2(-1, 0)))
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

    // The currently selected terrain tile is the reference for the rows below.
    kTerrain *selectedTerrain = nullptr;
    if (kObject *sel = manager->getActiveSelectedObject())
    {
        if (kMesh *selMesh = dynamic_cast<kMesh *>(sel))
        {
            for (const auto &pair : terrainMgr->getTiles())
            {
                kTerrain *t = pair.second.get();
                if (t && t->getMesh() == selMesh)
                {
                    selectedTerrain = t;
                    break;
                }
            }
        }
    }

    if (selectedTerrain)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                           "Selected tile: (%d, %d)",
                           selectedTerrain->getGridX(), selectedTerrain->getGridZ());
    else
        ImGui::TextDisabled("Select a terrain tile to add or remove its neighbours.");

    ImGui::Spacing();

    ImGui::InputFloat("Default Height", &m_defaultHeight);

    ImGui::Spacing();

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

    // --- Sculpting Tools ---
    ImGui::SeparatorText("Sculpting");
    ImGui::Checkbox("Sculpt Mode Active", &sculpt.active);
    if (sculpt.active)
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Hold left mouse in viewport to sculpt");
    }

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

    // --- Splat / Height Painting ---
    ImGui::SeparatorText("Painting");
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

    ImGui::SameLine();
    ImGui::Checkbox("Paint Height", &paint.heightActive);
    if (paint.heightActive)
    {
        sculpt.active = false;
        paint.active = false;
        paint.channel = -1;
    }

    if (paint.active)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click paint, R-click erase mask channel. Ctrl+click=sculpt");
    if (paint.heightActive)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                           "L-click lower, R-click raise height");

    ImGui::Spacing();

    // --- Noise Generation ---
    ImGui::SeparatorText("Noise Generation");
    ImGui::SliderFloat("Scale##noise", &m_noiseScale, 1.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("Amplitude##noise", &m_noiseAmplitude, 0.1f, 100.0f, "%.1f");
    ImGui::SliderInt("Octaves##noise", &m_noiseOctaves, 1, 8);
    const char *noiseTypes[] = {"fBm (smooth)", "Ridged (mountains)", "Billowed (clouds)"};
    ImGui::Combo("Type##noise", &m_noiseType, noiseTypes, IM_ARRAYSIZE(noiseTypes));
    ImGui::InputInt("Seed##noise", &m_noiseSeed);
    ImGui::Checkbox("Additive##noise", &m_noiseAdditive);

    // Noise target tile
    static int noiseGridX = 0;
    static int noiseGridZ = 0;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile X##noise", &noiseGridX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Tile Z##noise", &noiseGridZ);

    kTerrain *noiseTile = terrainMgr->getTile(noiseGridX, noiseGridZ);
    if (!noiseTile)
        ImGui::TextDisabled("No tile at (%d, %d). Create one below.", noiseGridX, noiseGridZ);

    if (ImGui::Button("Apply Noise", ImVec2(-1, 0)) && noiseTile)
    {
        beginTerrainEdit(noiseTile, true, false);
        applyNoiseToTile(noiseTile, m_noiseScale, m_noiseAmplitude,
                         m_noiseOctaves, m_noiseSeed, m_noiseAdditive);
        commitTerrainEdit();
        if (manager && manager->getRenderer())
            manager->getRenderer()->setOctreeDirty();
    }

    ImGui::Spacing();

    // --- Erosion ---
    ImGui::SeparatorText("Erosion");
    ImGui::SliderInt("Droplets##erode", &m_erodeDroplets, 1000, 500000, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Erode Radius##erode", &m_erodeRadius, 1.0f, 16.0f, "%.1f");
    ImGui::SliderFloat("Inertia##erode", &m_erodeInertia, 0.0f, 0.99f, "%.2f");
    ImGui::SliderFloat("Capacity##erode", &m_erodeCapacity, 1.0f, 16.0f, "%.1f");
    ImGui::SliderFloat("Deposit##erode", &m_erodeDeposit, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Erode Strength##erode", &m_erodeErode, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Evaporate##erode", &m_erodeEvaporate, 0.0f, 0.1f, "%.3f");
    ImGui::SliderFloat("Gravity##erode", &m_erodeGravity, 0.1f, 20.0f, "%.1f");

    if (ImGui::Button("Erode (Hydraulic)", ImVec2(-1, 0)) && noiseTile)
    {
        beginTerrainEdit(noiseTile, true, false);
        applyErosionToTile(noiseTile, m_erodeDroplets, m_erodeRadius, m_erodeInertia,
                           m_erodeCapacity, m_erodeDeposit, m_erodeErode,
                           m_erodeEvaporate, m_erodeGravity, 0.01f,
                           static_cast<unsigned int>(m_noiseSeed));
        commitTerrainEdit();
        if (manager && manager->getRenderer())
            manager->getRenderer()->setOctreeDirty();
    }

    ImGui::SliderInt("Thermal Iterations##erode", &m_thermalIterations, 1, 64);
    ImGui::SliderFloat("Talus##erode", &m_thermalTalus, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Thermal Strength##erode", &m_thermalStrength, 0.0f, 1.0f, "%.2f");
    if (ImGui::Button("Erode (Thermal)", ImVec2(-1, 0)) && noiseTile)
    {
        beginTerrainEdit(noiseTile, true, false);
        applyThermalErosionToTile(noiseTile, m_thermalIterations, m_thermalTalus, m_thermalStrength);
        commitTerrainEdit();
        if (manager && manager->getRenderer())
            manager->getRenderer()->setOctreeDirty();
    }

    ImGui::SliderFloat("Wind Direction##erode", &m_windDir, 0.0f, 360.0f, "%.0f");
    ImGui::SliderFloat("Wind Strength##erode", &m_windStrength, 0.0f, 1.0f, "%.2f");
    if (ImGui::Button("Erode (Wind)", ImVec2(-1, 0)) && noiseTile)
    {
        beginTerrainEdit(noiseTile, true, false);
        applyWindErosionToTile(noiseTile, m_windDir * 3.14159265f / 180.0f,
                               m_windStrength, 8, m_thermalTalus);
        commitTerrainEdit();
        if (manager && manager->getRenderer())
            manager->getRenderer()->setOctreeDirty();
    }

    ImGui::SliderInt("Hydro Iterations##erode", &m_hydroIterations, 1, 32);
    ImGui::SliderFloat("Hydro K##erode", &m_hydroK, 0.001f, 0.2f, "%.3f");
    ImGui::SliderFloat("Hydro M##erode", &m_hydroM, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("Hydro N##erode", &m_hydroN, 0.5f, 2.0f, "%.2f");
    ImGui::SliderFloat("Hydro Deposit##erode", &m_hydroDeposit, 0.0f, 0.9f, "%.2f");
    if (ImGui::Button("Carve Rivers (Hydrology)", ImVec2(-1, 0)) && noiseTile)
    {
        beginTerrainEdit(noiseTile, true, false);
        applyHydrologyToTile(noiseTile, m_hydroIterations, m_hydroK, m_hydroM,
                             m_hydroN, m_hydroDeposit, 0.01f);
        commitTerrainEdit();
        if (manager && manager->getRenderer())
            manager->getRenderer()->setOctreeDirty();
    }

    ImGui::Spacing();

    // Loading is fully automatic and has no user interface — the editor keeps
    // every tile's mesh resident and never unloads tiles.
    ensureTilesLoaded();

    ImGui::Spacing();

    // --- File Operations ---
    ImGui::SeparatorText("File Operations");

    if (ImGui::Button("Save All Terrains", ImVec2(-1, 0)))
    {
        saveProjectTerrains();
    }

    if (ImGui::Button("Load Terrains from Project", ImVec2(-1, 0)))
    {
        loadProjectTerrains();
    }

    ImGui::Spacing();

    // --- Layer Configuration ---
    ImGui::SeparatorText("Material Layers");

    static int selectedTileGridX = 0;
    static int selectedTileGridZ = 0;
    ImGui::InputInt("Tile Grid X##layer", &selectedTileGridX);
    ImGui::InputInt("Tile Grid Z##layer", &selectedTileGridZ);

    kTerrain *selectedTile = terrainMgr->getTile(selectedTileGridX, selectedTileGridZ);
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
                static char pathBuf[512];
                auto pathField = [&](const char *label, kString &path)
                {
                    strncpy_s(pathBuf, sizeof(pathBuf), path.c_str(), _TRUNCATE);
                    kString id = kString(label) + "##tex" + std::to_string(i);
                    if (ImGui::InputText(id.c_str(), pathBuf, sizeof(pathBuf)))
                        path = pathBuf;
                };
                pathField("Albedo", layer.albedoPath);
                pathField("Normal", layer.normalPath);
                pathField("ORM", layer.ormPath);
                pathField("Mask", layer.maskPath);

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

    // Terrain data (height, splat) is stored in Library/Terrains/
    // using the mesh UUID as filename, linking terrain data to scene objects.
    fs::path libDir = manager->projectPath / "Library" / "Terrains";

    manager->terrainManager->loadFromDirectory(libDir.string());

    if (manager->getRenderer())
        manager->getRenderer()->setOctreeDirty();

    m_terrainDirectory = libDir.string();
}

void PanelTerrain::saveProjectTerrains() const
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
    std::cerr << "saveProjectTerrains: saving " << manager->terrainManager->getTotalCount() << " tiles to " << libDir << "\n";

    for (const auto &pair : manager->terrainManager->getTiles())
    {
        const kTerrain *tile = pair.second.get();
        kMesh *mesh = tile->getMesh();
        if (!mesh)
        {
            std::cerr << "saveProjectTerrains: tile has no mesh, skipping\n";
            continue;
        }

        kString uuid = mesh->getUuid();
        if (uuid.empty())
        {
            std::cerr << "saveProjectTerrains: mesh has empty uuid, skipping\n";
            continue;
        }

        kString hFile = (libDir / (uuid + ".height")).string();
        kString sFile = (libDir / (uuid + ".splat")).string();
        std::cerr << "saveProjectTerrains: saving " << hFile << " and " << sFile << "\n";
        tile->saveHeightData(hFile);
        tile->saveSplatMap(sFile);
    }
}