#pragma once

#include <string>

namespace gdr {
    // A single review. Levels can carry any number of these, so unlike the first
    // version of the database nothing here is unique.
    struct Review {
        // Stable id assigned on publication, used by the moderator workflow to
        // find the same review again. Empty for hand written entries.
        std::string id;
        std::string author;
        std::string text;

        // Optional. The level is identified by levelID; the name is only kept so
        // the public site can show something readable.
        int levelID = 0;
        std::string levelName;

        // ISO 8601 in UTC, e.g. "2026-09-27T19:20:00Z". Sorts correctly as plain
        // text, which is what the list view relies on.
        std::string date;
    };
}
