/**
 * @file kprefab.h
 * @brief Serialized object subtree saved as a .prefab JSON asset.
 */

#ifndef KPREFAB_H
#define KPREFAB_H

#include "kexport.h"
#include "kdatatype.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace kemena
{
    /**
     * @brief A serialized object subtree saved as a .prefab JSON asset.
     *
     * A prefab stores a tree of objects (transforms, scripts, components) rooted
     * at a single object. Multiple prefab instances can be placed in scenes;
     * each instance gets fresh per-node UUIDs while remembering the template
     * UUID it was derived from, so the editor can match nodes across instances
     * and the template.
     *
     * On-disk format:
     * @code
     * {
     *   "type": "prefab",
     *   "uuid": "<asset uuid>",
     *   "name": "<display name>",
     *   "root": { ... full kObject::serialize() output ... }
     * }
     * @endcode
     */
    class KEMENA3D_API kPrefab
    {
    public:
        /** @brief Constructs an empty prefab with no UUID, name, or root subtree. */
        kPrefab();
        /** @brief Destroys the prefab and releases its stored JSON. */
        ~kPrefab();

        /**
         * @brief Loads a .prefab JSON file from disk into this prefab.
         * @param path Filesystem path to the .prefab file.
         * @return true on success, false if the file is missing or unparseable.
         */
        bool loadFromFile(const kString &path);

        /**
         * @brief Writes this prefab to a .prefab JSON file.
         * @param path Filesystem path to write to.
         * @return true on success, false if the file cannot be opened.
         */
        bool saveToFile(const kString &path);

        /** @brief Returns the prefab asset UUID. */
        kString getUuid() const          { return uuid; }
        /** @brief Sets the prefab asset UUID. */
        void    setUuid(const kString &v){ uuid = v; }
        /** @brief Returns the display name of the prefab. */
        kString getName() const          { return name; }
        /** @brief Sets the display name of the prefab. */
        void    setName(const kString &v){ name = v; }

        /** @brief Returns the object subtree JSON (the "root" field). */
        const json &getRootJson() const { return rootJson; }
        /** @brief Returns a mutable reference to the object subtree JSON (the "root" field). */
        json       &getRootJson()       { return rootJson; }
        /** @brief Replaces the object subtree JSON. */
        void        setRootJson(const json &j) { rootJson = j; }

        /**
         * @brief Generates a fresh-uuid copy of an object subtree.
         *
         * Walks the JSON, replaces every "uuid" with a freshly generated UUID,
         * and stores the original UUID into "template_uuid". Used when
         * instantiating a prefab into a scene so each instance has its own
         * identity but can still be matched back to the template.
         *
         * @param source       Object subtree (output of kObject::serialize()).
         * @param templateMap  Optional out: maps template_uuid -> instance_uuid
         *                     for every node in the subtree.
         * @return Renamed copy of @p source, ready to be deserialized into a scene.
         */
        static json instantiateJson(const json &source,
                                    std::unordered_map<kString, kString> *templateMap = nullptr);

    private:
        kString uuid;
        kString name;
        json    rootJson;
    };
}

#endif // KPREFAB_H
