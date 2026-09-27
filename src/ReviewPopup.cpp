#include "ReviewPopup.hpp"

#include <Geode/ui/Button.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr float PopupWidth = 380.f;
    constexpr float PopupHeight = 320.f;
    constexpr float SidePadding = 18.f;
    constexpr float TitleOffset = 18.f;
    constexpr float TitleGap = 6.f;
    constexpr float BottomPadding = 10.f;
    constexpr float ButtonGap = 16.f;

    // The bundled bitmap fonts only cover ASCII, so Cyrillic needs a TTF
    constexpr char const* FontFile = "Rubik.ttf";
    constexpr float TitleSize = 18.f;
    constexpr float MinTitleSize = 12.f;
    constexpr int MaxTitleLines = 2;
    constexpr float BodySize = 13.f;
    constexpr float HeaderSize = 12.f;
    constexpr float StatusSize = 11.f;
    constexpr float BlockGap = 10.f;
    constexpr float DividerHeight = 1.f;
    constexpr float MinViewHeight = 60.f;

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

    std::string urlEncode(std::string const& text) {
        static char const* digits = "0123456789ABCDEF";
        std::string out;
        out.reserve(text.size() * 3);

        for (unsigned char c : text) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += digits[c >> 4];
                out += digits[c & 0x0F];
            }
        }
        return out;
    }

    // "2026-09-27T19:20:00Z" -> "27.09.2026". Anything unexpected shows no date
    // rather than a wrong one.
    std::string formatDate(std::string const& date) {
        if (date.size() < 10 || date[4] != '-' || date[7] != '-') return "";
        return date.substr(8, 2) + "." + date.substr(5, 2) + "." + date.substr(0, 4);
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

    // One review: who wrote it and when on the first line, the text underneath
    struct Block {
        CCLabelTTF* header = nullptr;
        CCLabelTTF* stamp = nullptr;
        CCLabelTTF* body = nullptr;
        CCLayerColor* divider = nullptr;
        float height = 0.f;
    };

    // Geode buttons own their click behaviour, so a TTF label is handed over as
    // the display node instead of the bitmap font they expect
    geode::Button* textButton(std::string const& text, std::string const& font, ccColor3B colour, geode::Function<void(geode::Button*)> onClick) {
        auto label = CCLabelTTF::create(text.c_str(), font.c_str(), StatusSize + 1.f);
        label->setFontFillColor(colour);
        return geode::Button::createWithNode(label, std::move(onClick));
    }
}

namespace gdr {
    bool ReviewPopup::init(
        std::string const& title, LevelRef const& level,
        std::vector<Review> const& reviews, std::string const& status, bool statusIsError
    ) {
        if (!geode::Popup::init(PopupWidth, PopupHeight)) return false;

        m_level = level;

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

        auto top = TitleOffset + titleLabel->getContentSize().height + TitleGap;

        // A failed sync is stated outright, so a broken database is never silent
        if (!status.empty()) {
            auto statusLabel = CCLabelTTF::create(
                status.c_str(), font.c_str(), StatusSize, CCSize(contentWidth, 0.f),
                kCCTextAlignmentCenter, kCCVerticalTextAlignmentTop
            );
            statusLabel->setFontFillColor(
                statusIsError ? ccColor3B{ 255, 130, 130 } : ccColor3B{ 160, 160, 160 }
            );
            statusLabel->setAnchorPoint({ 0.5f, 1.f });
            m_mainLayer->addChildAtPosition(statusLabel, Anchor::Top, { 0.f, -top });
            top += statusLabel->getContentSize().height + TitleGap;
        }

        auto barTop = BottomPadding + StatusSize + 8.f;
        auto view = CCSize(contentWidth, PopupHeight - top - barTop);
        if (view.height < MinViewHeight) view.height = MinViewHeight;

        auto scroll = geode::ScrollLayer::create(view, true, true);
        // ScrollLayer clips content by treating the node origin as the bottom left
        // corner, which only lines up when the node is anchored there too.
        scroll->setAnchorPoint({ 0.f, 0.f });
        m_mainLayer->addChildAtPosition(scroll, Anchor::BottomLeft, { SidePadding, barTop });
        scroll->scrollToTop();

        auto content = scroll->m_contentLayer;

        if (reviews.empty()) {
            auto message = CCLabelTTF::create(
                "Nobody has reviewed this level yet.", font.c_str(), BodySize,
                CCSize(contentWidth, 0.f), kCCTextAlignmentLeft, kCCVerticalTextAlignmentTop
            );
            message->setFontFillColor({ 190, 190, 190 });
            message->setAnchorPoint({ 0.f, 1.f });

            auto height = message->getContentSize().height;
            scroll->setContentLayerSize(CCSize(contentWidth, height));
            content->addChild(message);
            message->setPosition({ 0.f, height });
        } else {
            // Built first, positioned afterwards: the scroll layer needs the
            // total height before anything can be placed from the top down.
            std::vector<Block> blocks;
            float total = 0.f;

            for (auto const& review : reviews) {
                Block block;

                auto author = trim(stripTags(review.author));
                if (!author.empty()) {
                    block.header = CCLabelTTF::create(
                        author.c_str(), font.c_str(), HeaderSize, CCSizeZero,
                        kCCTextAlignmentLeft, kCCVerticalTextAlignmentTop
                    );
                    block.header->setFontFillColor({ 255, 214, 130 });
                    block.header->setAnchorPoint({ 0.f, 1.f });
                }

                if (auto date = formatDate(review.date); !date.empty()) {
                    block.stamp = CCLabelTTF::create(
                        date.c_str(), font.c_str(), StatusSize, CCSizeZero,
                        kCCTextAlignmentRight, kCCVerticalTextAlignmentBottom
                    );
                    block.stamp->setFontFillColor({ 130, 130, 130 });
                    block.stamp->setAnchorPoint({ 1.f, 1.f });
                }

                block.body = CCLabelTTF::create(
                    wrap(trim(stripTags(review.text)), contentWidth, BodySize, measurer).c_str(),
                    font.c_str(), BodySize, CCSizeZero,
                    kCCTextAlignmentLeft, kCCVerticalTextAlignmentTop
                );
                block.body->setFontFillColor({ 225, 225, 225 });
                block.body->setAnchorPoint({ 0.f, 1.f });

                auto headerHeight = block.header ? block.header->getContentSize().height + 3.f : 0.f;
                block.height = headerHeight + block.body->getContentSize().height;

                total += block.height;
                blocks.push_back(block);
            }

            total += BlockGap * static_cast<float>(blocks.size() - 1);
            scroll->setContentLayerSize(CCSize(contentWidth, total));

            auto cursor = total;
            for (std::size_t i = 0; i < blocks.size(); ++i) {
                auto& block = blocks[i];
                cursor -= block.height;

                auto headerHeight = block.header ? block.header->getContentSize().height + 3.f : 0.f;

                // cursor is the top edge of the block, and both labels hang from it
                if (block.header) {
                    content->addChild(block.header);
                    block.header->setPosition({ 0.f, cursor });
                }
                if (block.stamp) {
                    content->addChild(block.stamp);
                    block.stamp->setPosition({ contentWidth, cursor });
                }

                content->addChild(block.body);
                block.body->setPosition({ 0.f, cursor - headerHeight });

                if (i + 1 < blocks.size()) {
                    block.divider = CCLayerColor::create(ccc4(255, 255, 255, 26), contentWidth, DividerHeight);
                    content->addChild(block.divider);
                    block.divider->setPosition({ 0.f, cursor - block.height - BlockGap / 2.f });
                }
            }
        }

        // Bottom bar: propose a review, or browse the whole database
        auto buttonColour = ccColor3B{ 130, 200, 255 };
        auto suggest = textButton("Suggest a review", font, buttonColour, [this](auto*) { submitReview(); });
        auto browse = textButton("All reviews", font, buttonColour, [this](auto*) { openSite(); });

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        menu->addChild(suggest);
        menu->addChild(browse);
        m_mainLayer->addChild(menu);

        auto suggestSize = suggest->getContentSize();
        auto browseSize = browse->getContentSize();
        auto barY = BottomPadding;

        suggest->setPosition({ PopupWidth / 2.f - (browseSize.width + ButtonGap) / 2.f, barY });
        browse->setPosition({ PopupWidth / 2.f + (suggestSize.width + ButtonGap) / 2.f, barY });

        return true;
    }

    std::string ReviewPopup::submitURL() const {
        auto base = Mod::get()->getSettingValue<std::string>("submit-url");
        if (base.empty()) return {};

        auto player = GameManager::get()->m_playerName;
        std::string author = player.empty() ? "" : std::string(player);
        if (author.empty()) author = "Anonymous";

        auto title = "Review: " + flatten(stripTags(m_level.name));

        // The player only has to type the review itself, everything else is
        // filled in so the moderator does not have to look anything up
        std::string body = "### Review submission\n\n"
                           "Level: " + flatten(stripTags(m_level.name)) + "\n"
                           "Level ID: " + std::to_string(m_level.id) + "\n"
                           "Author: " + author + "\n\n"
                           "Write the review under the line below.\n\n"
                           "---\n\n";

        return base
               + (base.find('?') == std::string::npos ? "?" : "&")
               + "labels=review-submission&title=" + urlEncode(title)
               + "&body=" + urlEncode(body);
    }

    void ReviewPopup::submitReview() {
        auto url = submitURL();
        if (url.empty()) {
            log::warn("No submit-url configured, cannot propose a review");
            FLAlertLayer::create(
                "GD Reviews",
                "Review submissions are not configured. Set a submit URL in the mod settings.",
                "OK"
            )->show();
            return;
        }

        web::openLinkInBrowser(url);
    }

    void ReviewPopup::openSite() {
        auto url = Mod::get()->getSettingValue<std::string>("site-url");
        if (url.empty()) return;
        web::openLinkInBrowser(url);
    }

    ReviewPopup* ReviewPopup::create(
        std::string const& title, LevelRef const& level,
        std::vector<Review> const& reviews, std::string const& status, bool statusIsError
    ) {
        auto ret = new ReviewPopup();
        if (ret->init(title, level, reviews, status, statusIsError)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
}
