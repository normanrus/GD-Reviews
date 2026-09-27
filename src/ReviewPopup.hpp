#pragma once

#include <Geode/Geode.hpp>

namespace gdr {
    class ReviewPopup : public geode::Popup {
    protected:
        bool init(std::string const& title, std::string const& author, std::string const& text);

    public:
        static ReviewPopup* create(
            std::string const& title, std::string const& author, std::string const& text
        );
    };
}
