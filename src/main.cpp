#include "ReviewPopup.hpp"
#include "ReviewStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

namespace {
    std::string levelName(GJGameLevel* level) {
        if (level->m_levelName.empty()) return "Level " + std::to_string(level->m_levelID);
        return level->m_levelName;
    }

    std::string reviewCount(std::size_t count) {
        if (count == 0) return "No reviews yet";
        return std::to_string(count) + (count == 1 ? " review" : " reviews");
    }
}

class $modify(MyLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        // Loose PNGs are not registered as sprite frames, so this has to go
        // through the mod's own "<id>/<file>" resource path as a texture.
        auto spr = geode::CircleButtonSprite::createWithSprite(
            Mod::get()->expandSpriteName("review_icon.png").c_str(),
            0.75f,
            geode::CircleBaseColor::DarkPurple,
            geode::CircleBaseSize::Medium
        );

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MyLevelInfoLayer::onReview));
        btn->setID("review-button"_spr);

        if (auto menu = typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu"))) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            log::error("left-side-menu not found, review button not added");
        }

        return true;
    }

    void onReview(CCObject*) {
        if (!m_level) {
            FLAlertLayer::create("GD Reviews", "Could not determine the level.", "OK")->show();
            return;
        }

        auto& store = gdr::ReviewStore::get();
        store.sync();

        auto const& reviews = store.forLevel(m_level->m_levelID);

        // A failed sync is shown rather than hidden, otherwise an empty popup
        // looks exactly like a level nobody has reviewed
        auto failed = !store.lastError().empty();

        gdr::ReviewPopup::create(
            levelName(m_level),
            gdr::LevelRef{ m_level->m_levelID, levelName(m_level) },
            reviews,
            failed ? store.lastError() : reviewCount(reviews.size()),
            failed
        )->show();
    }
};
