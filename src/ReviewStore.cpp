#include "ReviewStore.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

#include <charconv>
#include <chrono>
#include <filesystem>
#include <sstream>

using namespace geode::prelude;

namespace {
    using Reviews = std::unordered_map<int, gdr::Review>;

    constexpr std::string_view CacheName = "reviews-cache.json";
    constexpr std::string_view LocalName = "reviews.txt";
    constexpr std::chrono::minutes CacheLifetime{15};

    std::filesystem::path savePath(std::string_view name) {
        return Mod::get()->getSaveDir() / name;
    }

    std::optional<int> parseLevelID(std::string_view text) {
        int levelID = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), levelID);
        if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
        return levelID;
    }

    Reviews parse(matjson::Value const& json) {
        Reviews reviews;
        if (!json.isObject()) return reviews;

        for (auto const& [key, value] : json) {
            auto levelID = parseLevelID(key);
            if (!levelID) continue;

            // "id": "text" is accepted too, so one-line reviews stay readable
            if (value.isString()) {
                reviews[*levelID] = gdr::Review{ "", value.asString().unwrap() };
                continue;
            }
            if (!value.isObject() || !value.contains("text")) continue;

            auto author = value["author"].isString() ? value["author"].asString().unwrap() : "";
            auto text = value["text"].isString() ? value["text"].asString().unwrap() : "";
            if (!text.empty()) reviews[*levelID] = gdr::Review{ author, text };
        }

        return reviews;
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

        // "<id>|<text>" is still accepted for reviews written without an author
        auto rest = line.substr(idEnd + 1);
        auto authorEnd = rest.find('|');

        if (authorEnd == std::string::npos) {
            reviews[*levelID] = gdr::Review{ "", rest };
        } else {
            reviews[*levelID] = gdr::Review{ rest.substr(0, authorEnd), rest.substr(authorEnd + 1) };
        }
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

    std::optional<Review> ReviewStore::find(int levelID) const {
        auto it = m_reviews.find(levelID);
        if (it == m_reviews.end()) return std::nullopt;
        return it->second;
    }

    bool ReviewStore::synced() const {
        if (!m_syncedAt) return false;

        auto age = std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t(*m_syncedAt);
        return age < CacheLifetime;
    }

    void ReviewStore::readCache() {
        auto raw = file::readJson(savePath(CacheName));
        if (!raw) return;

        auto const& json = raw.unwrap();
        if (json.contains("syncedAt") && json["syncedAt"].isNumber()) {
            m_syncedAt = static_cast<std::time_t>(json["syncedAt"].asInt().unwrapOr(0));
        }
        if (json.contains("reviews")) m_reviews = parse(json["reviews"]);
    }

    void ReviewStore::readLocalFile() {
        // The bundled file ships with the mod, the one in the save directory is
        // the player's own copy and therefore takes precedence.
        mergeFile(m_reviews, Mod::get()->getResourcesDir() / LocalName);
        mergeFile(m_reviews, savePath(LocalName));
    }

    void ReviewStore::writeCache(matjson::Value const& reviews) {
        matjson::Value root = matjson::Value::object();
        root["syncedAt"] = static_cast<std::intmax_t>(std::time(nullptr));
        root["reviews"] = reviews;

        auto res = file::writeString(savePath(CacheName), root.dump());
        if (!res) log::warn("Could not write review cache: {}", res.unwrapErr());
    }

    void ReviewStore::sync() {
        if (synced()) return;

        auto url = Mod::get()->getSettingValue<std::string>("reviews-url");
        if (url.empty()) {
            log::warn("No reviews-url configured, skipping sync");
            return;
        }

        auto response = web::WebRequest()
            .timeout(std::chrono::seconds(5))
            .userAgent("GD Reviews mod")
            .getSync(url);

        if (!response.ok()) {
            log::warn("Review sync failed: {}", response.errorMessage());
            return;
        }

        auto json = response.json();
        if (!json || !json.unwrap().isObject()) {
            log::warn("Review sync returned malformed JSON");
            return;
        }

        auto& downloaded = json.unwrap();
        m_reviews = parse(downloaded);
        readLocalFile();
        m_syncedAt = std::time(nullptr);
        writeCache(downloaded);

        log::info("Synced {} reviews from {}", m_reviews.size(), url);
    }
}
