#include "ReviewStore.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <sstream>

using namespace geode::prelude;

namespace {
    using Reviews = std::unordered_map<int, std::vector<gdr::Review>>;

    constexpr std::string_view CacheName = "reviews-cache.json";
    constexpr std::string_view LocalName = "reviews.txt";

    // 0 means the database is downloaded on every button press, which is what you
    // want while editing it
    std::chrono::minutes cacheLifetime() {
        auto minutes = Mod::get()->getSettingValue<int>("cache-minutes");
        return std::chrono::minutes(std::clamp(minutes, 0, 1440));
    }

    std::chrono::system_clock::duration cacheAge(std::time_t syncedAt) {
        return std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t(syncedAt);
    }

    std::filesystem::path savePath(std::string_view name) {
        return Mod::get()->getSaveDir() / name;
    }

    std::optional<int> parseLevelID(std::string_view text) {
        int levelID = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), levelID);
        if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
        return levelID;
    }

    std::string field(matjson::Value const& value, std::string_view key) {
        if (!value.isObject() || !value.contains(key)) return "";
        auto const& entry = value[key];
        if (!entry.isString()) return "";
        return entry.asString().unwrapOr("");
    }

    // ASCII and Cyrillic are both folded, because the bundled font renders the
    // second one and searches should not care about capitals
    char fold(char c) {
        auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') return static_cast<char>(u + 0x20);
        if (u >= 0xC0 && u <= 0xDE && u != 0xD7) return static_cast<char>(u + 0x20);
        return static_cast<char>(u);
    }

    bool containsFold(std::string const& haystack, std::string const& needle) {
        if (needle.empty()) return true;
        if (needle.size() > haystack.size()) return false;

        std::string subject;
        subject.reserve(haystack.size());
        for (char c : haystack) subject += fold(c);

        return subject.find(needle) != std::string::npos;
    }

    std::string folded(std::string const& text) {
        std::string out;
        out.reserve(text.size());
        for (char c : text) out += fold(c);
        return out;
    }

    // A review is only identified by what a reader sees, so the same line
    // appearing in both the bundled and the player's file is not a second review
    bool sameReview(gdr::Review const& a, gdr::Review const& b) {
        return a.levelID == b.levelID && a.author == b.author && a.text == b.text;
    }

    void add(Reviews& reviews, gdr::Review review) {
        auto& list = reviews[review.levelID];
        for (auto const& existing : list) {
            if (sameReview(existing, review)) return;
        }
        list.push_back(std::move(review));
    }

    // Entries without a date are hand written, so they are shown last instead of
    // pretending to be ancient
    void sortNewestFirst(std::vector<gdr::Review>& list) {
        std::stable_sort(list.begin(), list.end(), [](gdr::Review const& a, gdr::Review const& b) {
            if (a.date.empty() != b.date.empty()) return !a.date.empty();
            if (a.date != b.date) return a.date > b.date;
            return a.author < b.author;
        });
    }

    struct Parsed {
        Reviews reviews;
        std::size_t seen = 0;
        std::size_t skipped = 0;

        // False when the document is not shaped like a database at all, which is
        // the one case that cannot be worked around by skipping entries
        bool usable = false;
    };

    gdr::Review readEntry(matjson::Value const& value, int levelID) {
        gdr::Review review;
        review.levelID = levelID;
        review.levelName = field(value, "levelName");
        review.author = field(value, "author");
        review.text = field(value, "text");
        review.date = field(value, "date");

        // The published id is optional, older databases do not have one
        review.id = field(value, "id");

        return review;
    }

    void readArray(Reviews& reviews, matjson::Value const& array, Parsed& out) {
        for (auto const& value : array) {
            out.seen++;

            int levelID = 0;
            if (value.isObject()) {
                if (value.contains("levelId") && value["levelId"].isNumber()) {
                    levelID = static_cast<int>(value["levelId"].asInt().unwrapOr(0));
                } else if (value.contains("levelID") && value["levelID"].isNumber()) {
                    levelID = static_cast<int>(value["levelID"].asInt().unwrapOr(0));
                }
            }

            auto review = readEntry(value, levelID);
            if (levelID <= 0 || review.text.empty()) {
                out.skipped++;
                continue;
            }
            add(reviews, std::move(review));
        }
    }

    // The first database was a map of level id to a single review. It is still
    // accepted so a database that was never migrated keeps working.
    void readMap(Reviews& reviews, matjson::Value const& object, Parsed& out) {
        for (auto const& [key, value] : object) {
            out.seen++;

            auto levelID = parseLevelID(key);
            if (!levelID) {
                out.skipped++;
                continue;
            }

            gdr::Review review;
            review.levelID = *levelID;

            if (value.isString()) {
                // "id": "text" is accepted too, so one-line reviews stay readable
                review.text = value.asString().unwrapOr("");
            } else if (value.isObject()) {
                review = readEntry(value, *levelID);
            }

            if (review.text.empty()) {
                out.skipped++;
                continue;
            }
            add(reviews, std::move(review));
        }
    }

    Parsed parse(matjson::Value const& json) {
        Parsed out;

        if (json.isArray()) {
            out.usable = true;
            readArray(out.reviews, json, out);
        } else if (json.isObject()) {
            out.usable = true;
            if (json.contains("reviews")) {
                auto const& inner = json["reviews"];
                if (inner.isArray()) {
                    readArray(out.reviews, inner, out);
                } else if (inner.isObject()) {
                    readMap(out.reviews, inner, out);
                } else {
                    out.usable = false;
                }
            } else {
                readMap(out.reviews, json, out);
            }
        }

        for (auto& [levelID, list] : out.reviews) sortNewestFirst(list);
        return out;
    }

    void mergeLine(Reviews& reviews, std::string const& line) {
        if (line.empty() || line[0] == '#') return;

        auto idEnd = line.find('|');
        if (idEnd == std::string::npos) {
            log::warn("Malformed review line: {}", line);
            return;
        }

        auto levelID = parseLevelID(std::string_view(line).substr(0, idEnd));
        if (!levelID) {
            log::warn("Malformed level id in review line: {}", line);
            return;
        }

        gdr::Review review;
        review.levelID = *levelID;

        // "<id>|<text>" is still accepted for reviews written without an author
        auto rest = line.substr(idEnd + 1);
        auto authorEnd = rest.find('|');

        if (authorEnd == std::string::npos) {
            review.text = std::move(rest);
        } else {
            review.author = rest.substr(0, authorEnd);
            review.text = rest.substr(authorEnd + 1);
        }

        if (review.text.empty()) return;
        add(reviews, std::move(review));
    }

    void mergeFile(Reviews& reviews, std::filesystem::path const& path) {
        if (!std::filesystem::exists(path)) return;

        auto raw = file::readString(path);
        if (!raw) {
            log::warn("Could not read {}: {}", path.string(), raw.unwrapErr());
            return;
        }

        std::istringstream stream(raw.unwrap());
        for (std::string line; std::getline(stream, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            mergeLine(reviews, line);
        }
    }
}

namespace gdr {
    ReviewStore& ReviewStore::get() {
        // Intentionally leaked: static destruction could run after the mod is unloaded
        static auto store = new ReviewStore();
        return *store;
    }

    ReviewStore::ReviewStore() {
        readCache();
        readLocalFile();
    }

    std::vector<Review> const& ReviewStore::forLevel(int levelID) const {
        static std::vector<Review> const empty;
        auto it = m_reviews.find(levelID);
        if (it == m_reviews.end()) return empty;
        return it->second;
    }

    std::vector<Review> ReviewStore::search(std::string const& query) const {
        auto needle = folded(query);
        std::vector<Review> found;

        for (auto const& [levelID, list] : m_reviews) {
            for (auto const& review : list) {
                if (containsFold(review.author, needle) || containsFold(review.text, needle)
                    || containsFold(review.levelName, needle)) {
                    found.push_back(review);
                }
            }
        }

        std::stable_sort(found.begin(), found.end(), [](Review const& a, Review const& b) {
            if (a.date.empty() != b.date.empty()) return !a.date.empty();
            return a.date > b.date;
        });
        return found;
    }

    std::size_t ReviewStore::total() const {
        std::size_t count = 0;
        for (auto const& [levelID, list] : m_reviews) count += list.size();
        return count;
    }

    std::size_t ReviewStore::countFor(int levelID) const {
        auto it = m_reviews.find(levelID);
        if (it == m_reviews.end()) return 0;
        return it->second.size();
    }

    std::string const& ReviewStore::lastError() const {
        return m_lastError;
    }

    std::optional<std::time_t> ReviewStore::lastSyncedAt() const {
        return m_syncedAt;
    }

    bool ReviewStore::synced() const {
        if (!m_syncedAt) return false;

        auto lifetime = cacheLifetime();
        if (lifetime.count() == 0) return false;

        return cacheAge(*m_syncedAt) < lifetime;
    }

    void ReviewStore::readCache() {
        auto raw = file::readJson(savePath(CacheName));
        if (!raw) return;

        auto const& json = raw.unwrap();
        if (json.contains("syncedAt") && json["syncedAt"].isNumber()) {
            m_syncedAt = static_cast<std::time_t>(json["syncedAt"].asInt().unwrapOr(0));
        }
        if (json.contains("etag") && json["etag"].isString()) {
            m_etag = json["etag"].asString().unwrapOr("");
        }
        if (json.contains("reviews")) m_reviews = parse(json["reviews"]).reviews;
    }

    void ReviewStore::readLocalFile() {
        // The bundled file ships with the mod, the one in the save directory is
        // the player's own copy. Both are added, duplicates are dropped.
        mergeFile(m_reviews, Mod::get()->getResourcesDir() / LocalName);
        mergeFile(m_reviews, savePath(LocalName));

        for (auto& [levelID, list] : m_reviews) sortNewestFirst(list);
    }

    void ReviewStore::writeCache(matjson::Value const& reviews) {
        matjson::Value root = matjson::Value::object();
        root["syncedAt"] = static_cast<std::intmax_t>(std::time(nullptr));
        if (!m_etag.empty()) root["etag"] = m_etag;
        root["reviews"] = reviews;

        auto res = file::writeString(savePath(CacheName), root.dump());
        if (!res) log::warn("Could not write review cache: {}", res.unwrapErr());
    }

    void ReviewStore::sync(bool force) {
        if (!force && synced()) {
            auto spent = std::chrono::duration_cast<std::chrono::minutes>(cacheAge(*m_syncedAt));
            log::info(
                "Using {} cached reviews, next refresh in {} min",
                total(), cacheLifetime().count() - spent.count()
            );
            return;
        }

        auto url = Mod::get()->getSettingValue<std::string>("reviews-url");
        if (url.empty()) {
            m_lastError = "No database URL is configured.";
            log::warn("No reviews-url configured, skipping sync");
            return;
        }

        auto request = web::WebRequest()
                           .timeout(std::chrono::seconds(5))
                           .userAgent("Mozilla/5.0 (compatible; GD-Reviews/0.2)");

        // Asking with the stored validator lets GitHub answer "nothing changed"
        // without sending the database again
        if (!m_etag.empty()) request.header("If-None-Match", m_etag);

        auto response = request.getSync(url);

        if (response.code() == 304) {
            m_syncedAt = std::time(nullptr);
            m_lastError.clear();
            log::info("Database unchanged since last sync ({} reviews)", total());
            return;
        }

        if (!response.ok()) {
            m_lastError = fmt::format(
                "Could not reach the database (HTTP {}). Showing {} saved reviews.",
                response.code(), total()
            );
            log::warn(
                "Review sync failed: {} -> code={} info={} error={} cancelled={} ({})",
                url, response.code(), response.info(), response.error(),
                response.cancelled(), response.errorMessage()
            );
            log::info("Review sync request log: {}", response.verboseLogs());
            return;
        }

        auto json = response.json();
        if (!json) {
            m_lastError = "The downloaded database could not be read as JSON.";
            log::warn(
                "Review sync returned malformed JSON, code={} body={}",
                response.code(), response.string().unwrapOr("<unreadable>")
            );
            return;
        }

        auto parsed = parse(json.unwrap());
        if (!parsed.usable) {
            m_lastError = "The downloaded database is not shaped like a review database.";
            log::warn("Review sync got an unexpected document, keys={}", json.unwrap().dump());
            return;
        }

        // A handful of broken entries must not cost the reader every review
        if (parsed.skipped) {
            log::warn("Skipped {} of {} entries in the database", parsed.skipped, parsed.seen);
        }

        m_reviews = std::move(parsed.reviews);
        readLocalFile();
        m_syncedAt = std::time(nullptr);
        m_lastError.clear();

        // A response without a validator simply means the next sync is a full one
        if (auto etag = response.header("ETag")) {
            m_etag = std::string(*etag);
        } else {
            m_etag.clear();
        }

        writeCache(json.unwrap());

        log::info("Synced {} reviews from {}", total(), url);
    }
}
