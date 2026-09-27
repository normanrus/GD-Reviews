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
            gdr::ReviewPopup::create("Error", "", "Could not determine the level.")->show();
            return;
        }

        auto& store = gdr::ReviewStore::get();
        store.sync();

        auto review = store.find(m_level->m_levelID);
        gdr::ReviewPopup::create(
            levelName(m_level),
            review ? review->author : "",
            review ? review->text : "No review has been written for this level yet."
        )->show();
    }
};
