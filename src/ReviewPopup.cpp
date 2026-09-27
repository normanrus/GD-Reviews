#include "ReviewPopup.hpp"

#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr float PopupWidth = 340.f;
    constexpr float PopupHeight = 260.f;
    constexpr float SidePadding = 18.f;
    constexpr float TitleOffset = 20.f;
    constexpr float TitleGap = 8.f;
    constexpr float BottomPadding = 34.f;

    // The bundled bitmap fonts only cover ASCII, so Cyrillic needs a TTF
    constexpr char const* FontFile = "Rubik.ttf";
    constexpr float TitleSize = 18.f;
    constexpr float MinTitleSize = 12.f;
    constexpr int MaxTitleLines = 2;
    constexpr float BodySize = 14.f;
    constexpr float AuthorSize = 12.f;

    std::string fontFile() {
        return Mod::get()->expandSpriteName(FontFile);
    }

    // Colour tags are understood by FLAlertLayer, CCLabelTTF would print them
    std::string stripTags(std::string const& text) {
        std::string out;
        out.reserve(text.size());

        bool inTag = false;
        for (char c : text) {
            if (c == '<') {
                inTag = true;
            } else if (c == '>') {
                inTag = false;
            } else if (!inTag) {
                out += c;
            }
        }
        return out;
    }

    std::string trim(std::string text) {
        auto solid = [](unsigned char c) { return !std::isspace(c); };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), solid));
        text.erase(std::find_if(text.rbegin(), text.rend(), solid).base(), text.end());
        return text;
    }

    // Level names routinely carry line breaks and colour tags, so a title has to
    // end up as a short single paragraph
    std::string flatten(std::string const& text) {
        std::string out;
        out.reserve(text.size());

        for (char c : text) {
            out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        }

        std::string squeezed;
        squeezed.reserve(out.size());
        for (char c : out) {
            if (c == ' ' && !squeezed.empty() && squeezed.back() == ' ') continue;
            squeezed += c;
        }
        return trim(squeezed);
    }

    // Text measurement is the expensive part of wrapping, and the same strings
    // come up repeatedly while a popup is laid out
    class Measurer {
        std::string m_font;
        std::map<std::pair<float, std::string>, float> m_cache;

    public:
        explicit Measurer(std::string font) : m_font(std::move(font)) {}

        float at(std::string const& text, float size) {
            auto key = std::make_pair(size, text);
            if (auto it = m_cache.find(key); it != m_cache.end()) return it->second;

            auto label = CCLabelTTF::create(text.c_str(), m_font.c_str(), size);
            return m_cache.emplace(key, label->getContentSize().width).first->second;
        }
    };

    // Words wider than the whole popup have to be broken up character by character
    std::vector<std::string> split(std::string const& word, float maxWidth, float size, Measurer& measurer) {
        std::vector<std::string> parts;
        if (measurer.at(word, size) <= maxWidth) {
            parts.push_back(word);
            return parts;
        }

        std::string part;
        for (char c : word) {
            if (!part.empty() && measurer.at(part + c, size) > maxWidth) {
                parts.push_back(part);
                part.clear();
            }
            part += c;
        }
        parts.push_back(part);
        return parts;
    }

    std::string wrap(std::string const& text, float maxWidth, float size, Measurer& measurer) {
        std::string line;
        std::string out;
        auto flush = [&] {
            if (!out.empty()) out += '\n';
            out += line;
            line.clear();
        };

        std::string word;
        auto pushWord = [&] {
            if (word.empty()) return;
            for (auto const& part : split(word, maxWidth, size, measurer)) {
                auto candidate = line.empty() ? part : line + " " + part;
                if (!line.empty() && measurer.at(candidate, size) > maxWidth) {
                    flush();
                    candidate = part;
                }
                line = line.empty() ? part : candidate;
            }
            word.clear();
        };

        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') {
                pushWord();
                flush();
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                pushWord();
            } else {
                word += c;
            }
        }
        pushWord();
        flush();

        return trim(out);
    }

    int countLines(std::string const& text) {
        return 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
    }

    // Names far longer than the popup still have to stay within MaxTitleLines
    std::string clampLines(std::string const& text) {
        if (countLines(text) <= MaxTitleLines) return text;

        std::vector<std::string> lines;
        std::string line;
        for (char c : text) {
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            } else {
                line += c;
            }
        }
        lines.push_back(line);

        lines.resize(MaxTitleLines);
        lines.back() = trim(lines.back()) + "…";

        std::string out;
        for (auto const& kept : lines) {
            if (!out.empty()) out += '\n';
            out += kept;
        }
        return out;
    }

    // Long level names are shrunk rather than truncated, so the full name stays visible
    std::pair<std::string, float> fitTitle(std::string const& text, float maxWidth, Measurer& measurer) {
        for (float size = TitleSize; size > MinTitleSize; size -= 1.f) {
            auto wrapped = wrap(text, maxWidth, size, measurer);
            if (countLines(wrapped) <= MaxTitleLines) return { wrapped, size };
        }
        return { clampLines(wrap(text, maxWidth, MinTitleSize, measurer)), MinTitleSize };
    }
}

namespace gdr {
    bool ReviewPopup::init(std::string const& title, std::string const& author,
                           std::string const& text) {
        if (!geode::Popup::init(PopupWidth, PopupHeight)) return false;

        auto font = fontFile();
        auto measurer = Measurer(font);
        auto contentWidth = PopupWidth - SidePadding * 2.f;

        auto [titleText, titleSize] = fitTitle(flatten(stripTags(title)), contentWidth, measurer);
        auto titleLabel = CCLabelTTF::create(
            titleText.c_str(), font.c_str(), titleSize, CCSizeZero,
            kCCTextAlignmentCenter, kCCVerticalTextAlignmentTop
        );
        titleLabel->setFontFillColor({ 255, 255, 255 });
        m_mainLayer->addChildAtPosition(titleLabel, Anchor::Top, { 0.f, -TitleOffset });

        // A wrapped title eats into the review, so the body area is what is left over
        auto bodyTop = TitleOffset + titleLabel->getContentSize().height + TitleGap;
        auto view = CCSize(contentWidth, PopupHeight - bodyTop - BottomPadding);
        if (view.height < 40.f) {
            view.height = 40.f;
            bodyTop = PopupHeight - BottomPadding - view.height;
        }

        auto body = CCLabelTTF::create(
            wrap(trim(stripTags(text)), view.width, BodySize, measurer).c_str(), font.c_str(),
            BodySize, CCSizeZero, kCCTextAlignmentLeft, kCCVerticalTextAlignmentTop
        );
        body->setFontFillColor({ 230, 230, 230 });
        body->setAnchorPoint({ 0.f, 1.f });

        auto contentHeight = std::max(body->getContentSize().height, view.height);

        auto scroll = geode::ScrollLayer::create(view, true, true);
        // ScrollLayer clips content by treating the node origin as the bottom left
        // corner, which only lines up when the node is anchored there too.
        scroll->setAnchorPoint({ 0.f, 0.f });
        m_mainLayer->addChildAtPosition(scroll, Anchor::BottomLeft, { SidePadding, BottomPadding });

        scroll->setContentLayerSize(CCSize(view.width, contentHeight));
        scroll->scrollToTop();
        scroll->m_contentLayer->addChild(body);
        body->setPosition({ 0.f, contentHeight });

        auto cleanAuthor = trim(stripTags(author));
        if (!cleanAuthor.empty()) {
            auto authorLabel = CCLabelTTF::create(
                ("- " + cleanAuthor).c_str(), font.c_str(), AuthorSize, CCSizeZero,
                kCCTextAlignmentRight, kCCVerticalTextAlignmentBottom
            );
            authorLabel->setFontFillColor({ 165, 165, 165 });
            m_mainLayer->addChildAtPosition(authorLabel, Anchor::BottomRight, { -SidePadding, 12.f });
        }

        return true;
    }

    ReviewPopup* ReviewPopup::create(std::string const& title, std::string const& author,
                                     std::string const& text) {
        auto ret = new ReviewPopup();
        if (ret->init(title, author, text)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
}
