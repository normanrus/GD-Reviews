#pragma once

#include "Review.hpp"

#include <Geode/Geode.hpp>

#include <ctime>
#include <optional>
#include <unordered_map>

namespace gdr {
    // Reviews live in a JSON file hosted on GitHub Pages. The last successful
    // download is kept in the save directory so the popup still works offline.
    // reviews.txt is merged on top of the download, bundled first and the player's
    // own copy second, so reviews can be added or overridden without a redeploy.
    class ReviewStore {
    public:
        static ReviewStore& get();

        std::optional<Review> find(int levelID) const;

        // Downloads the database unless the cache is still fresh. Blocks for the
        // duration of the request, so it is only ever called on a button press.
        void sync();

    private:
        ReviewStore();

        // Whether the cached database is recent enough to be trusted.
        bool synced() const;

        void readCache();
        void readLocalFile();
        void writeCache(matjson::Value const& reviews);

        std::unordered_map<int, Review> m_reviews;
        std::optional<std::time_t> m_syncedAt;
    };
}
