#include <Geode/Geode.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

#include "jukebox/JukeboxIntegration.hpp"
#include "net/BackendClient.hpp"
#include "ui/RightsBrowserPopup.hpp"
#include "ui/RightsDetailPopup.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    mrc::net::BackendClient::get().initializeCache();
    log::info(
        "Music Rights Checker v0.1.1 initialized (Jukebox loaded: {}, version: {}).",
        mrc::jb::JukeboxIntegration::isJukeboxLoaded(),
        mrc::jb::JukeboxIntegration::jukeboxVersionString()
    );
}

$on_mod(DataSaved) {
    mrc::net::BackendClient::get().flushCache();
}

namespace {

/**
 * Integrates non-invasively with Jukebox's NONG management popup (`NongDropdownLayer`)
 * using public Geode Node IDs (`"NongList"`, `"buttons-menu"`, `"song-info-label"`, `"artist-label"`).
 * Adds a compact "MRC" rights-inspection button to Jukebox song rows and a "Rights Catalog" button
 * without modifying Jukebox's binary ABI or private data structures.
 */
class $modify(MRCJukeboxPopupHook, FLAlertLayer) {
    void show() {
        FLAlertLayer::show();

        if (!mrc::net::BackendClient::get().isModEnabled() ||
            !Mod::get()->getSettingValue<bool>("show-in-jukebox-list")) {
            return;
        }

        // Detect Jukebox's NongDropdownLayer by checking for its public child node ID "NongList"
        if (!this->m_mainLayer) {
            return;
        }
        CCNode* nongList = this->m_mainLayer->getChildByIDRecursive("NongList");
        if (!nongList) {
            return;
        }

        attachCatalogButtonIfMissing();
        attachRowButtons(nongList);
    }

    void attachCatalogButtonIfMissing() {
        if (!this->m_buttonMenu || this->m_buttonMenu->getChildByID("mrc-jukebox-catalog-btn"_spr)) {
            return;
        }
        auto* spr = ButtonSprite::create("Rights", "goldFont.fnt", "GJ_button_04.png", 0.5f);
        spr->setScale(0.65f);
        auto* btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MRCJukeboxPopupHook::onOpenRightsCatalog)
        );
        btn->setID("mrc-jukebox-catalog-btn"_spr);
        this->m_buttonMenu->addChildAtPosition(btn, Anchor::BottomLeft, {36.f, 22.f});
    }

    void attachRowButtons(CCNode* nongList) {
        CCNode* contentLayer = nongList->getChildByIDRecursive("content-layer");
        CCArray* children = contentLayer ? contentLayer->getChildren() : nongList->getChildren();
        if (!children) {
            return;
        }

        for (CCNode* rowNode : CCArrayExt<CCNode*>(children)) {
            if (!rowNode) continue;
            auto* buttonsMenu = typeinfo_cast<CCMenu*>(rowNode->getChildByIDRecursive("buttons-menu"));
            if (!buttonsMenu || buttonsMenu->getChildByID("mrc-row-rights-btn"_spr)) {
                continue;
            }

            auto* spr = ButtonSprite::create("MRC", "goldFont.fnt", "GJ_button_01.png", 0.48f);
            spr->setScale(0.58f);
            auto* btn = CCMenuItemSpriteExtra::create(
                spr,
                this,
                menu_selector(MRCJukeboxPopupHook::onInspectJukeboxRow)
            );
            btn->setID("mrc-row-rights-btn"_spr);
            btn->setUserObject("target-row"_spr, rowNode);
            buttonsMenu->addChild(btn);
            buttonsMenu->updateLayout();
        }
    }

    void onOpenRightsCatalog(CCObject*) {
        if (auto* popup = mrc::ui::RightsBrowserPopup::create()) {
            popup->show();
        }
    }

    void onInspectJukeboxRow(CCObject* sender) {
        auto* menuItem = typeinfo_cast<CCNode*>(sender);
        if (!menuItem) return;
        auto* rowNode = typeinfo_cast<CCNode*>(menuItem->getUserObject("target-row"_spr));
        if (!rowNode) return;

        mrc::SongQuery query;
        query.sourceType = mrc::SourceType::JukeboxLocal;

        if (auto* titleLabel = typeinfo_cast<CCLabelBMFont*>(rowNode->getChildByIDRecursive("song-info-label"))) {
            if (titleLabel->getString()) {
                query.title = titleLabel->getString();
            }
        }
        if (auto* artistLabel = typeinfo_cast<CCLabelBMFont*>(rowNode->getChildByIDRecursive("artist-label"))) {
            if (artistLabel->getString()) {
                query.artist = artistLabel->getString();
            }
        }
        if (auto* metaLabel = typeinfo_cast<CCLabelBMFont*>(rowNode->getChildByIDRecursive("metadata-label"))) {
            if (metaLabel->getString()) {
                std::string meta = metaLabel->getString();
                if (meta.find("youtube") != std::string::npos) {
                    query.sourceType = mrc::SourceType::JukeboxYoutube;
                } else if (meta.find("hosted") != std::string::npos || meta.find("sfh") != std::string::npos) {
                    query.sourceType = mrc::SourceType::JukeboxHosted;
                }
            }
        }

        if (query.title.empty()) {
            query.title = "Jukebox NONG Track";
        }
        if (query.artist.empty()) {
            query.artist = "Unknown Artist";
        }

        if (auto* detail = mrc::ui::RightsDetailPopup::create(std::move(query))) {
            detail->show();
        }
    }
};

}  // namespace
