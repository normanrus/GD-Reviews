#pragma once

#include "Review.hpp"

#include <Geode/Geode.hpp>

#include <ctime>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gdr {
    // Reviews live in a JSON file hosted on GitHub Pages. The last successful
    // download is kept in the save directory so the popup still works offline.
    // reviews.txt is merged on top of the download, bundled first and the player's
    // own copy second, so reviews can be added without a redeploy.
    //
    // A level can have any number of reviews. Everything is sorted newest first
    // so callers never have to think about ordering.
    class ReviewStore {
    public:
        static ReviewStore& get();

        // Reviews written for a level, newest first. The reference stays valid
        // until the next sync, and is empty when the level has none.
        std::vector<Review> const& forLevel(int levelID) const;

        // Every review whose author, level name or text contains query, matched
        // case insensitively. An empty query returns everything.
        std::vector<Review> search(std::string const& query) const;

        // How many reviews are loaded in total, and how many belong to one level.
        std::size_t total() const;
        std::size_t countFor(int levelID) const;

        // Why the last sync did not produce fresh data, or an empty string if it
        // did. The popup shows this so a broken database is never silent.
        std::string const& lastError() const;
        std::optional<std::time_t> lastSyncedAt() const;

        // Downloads the database unless the cache is still fresh. Blocks for the
        // duration of the request, so it is only ever called on a button press.
        // Pass force to ignore the cache lifetime.
        void sync(bool force = false);

    private:
        ReviewStore();

        // Whether the cached database is recent enough to be trusted.
        bool synced() const;

        void readCache();
        void readLocalFile();
        void writeCache(matjson::Value const& reviews);

        std::unordered_map<int, std::vector<Review>> m_reviews;
        std::optional<std::time_t> m_syncedAt;
        std::string m_lastError;
        std::string m_etag;
    };
}
