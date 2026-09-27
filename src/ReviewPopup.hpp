#pragma once

#include "Review.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace gdr {
    // A level can have several reviews, so the popup shows a list. The submit
    // button needs to know which level it was opened for.
    struct LevelRef {
        int id = 0;
        std::string name;
    };

    class ReviewPopup : public geode::Popup {
    protected:
        bool init(
            std::string const& title, LevelRef const& level,
            std::vector<Review> const& reviews, std::string const& status, bool statusIsError
        );

        void submitReview();
        void openSite();

        // Opens a prefilled issue so the player never has to touch the database
        // file itself. The moderator merges it once they approve the issue.
        std::string submitURL() const;

    public:
        static ReviewPopup* create(
            std::string const& title, LevelRef const& level,
            std::vector<Review> const& reviews, std::string const& status, bool statusIsError
        );

        LevelRef m_level;
    };
}
