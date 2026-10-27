#include "RightsBrowserPopup.hpp"
#include "RightsDetailPopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>

#include "../net/BackendClient.hpp"

namespace mrc::ui {

RightsBrowserPopup* RightsBrowserPopup::create() {
    auto* ret = new RightsBrowserPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RightsBrowserPopup::init() {
    if (!Popup::init(440.f, 280.f)) {
        return false;
    }

    this->setTitle("Browse & Filter Song Rights");
    const auto sz = m_mainLayer->getContentSize();

    m_searchInput = geode::TextInput::create(230.f, "Search title, artist, or song ID...");
    m_searchInput->setCommonFilter(geode::CommonFilter::Any);
    m_searchInput->setMaxCharCount(100);
    m_searchInput->setPosition({134.f, sz.height - 50.f});
    m_mainLayer->addChild(m_searchInput);

    auto addTopBtn = [&](const char* caption, const char* tex, cocos2d::SEL_MenuHandler sel, float xPos) {
        auto* spr = ButtonSprite::create(caption, "goldFont.fnt", tex, 0.52f);
        spr->setScale(0.75f);
        auto* item = CCMenuItemSpriteExtra::create(spr, this, sel);
        m_buttonMenu->addChildAtPosition(item, geode::Anchor::TopLeft, {xPos, -50.f});
    };

    addTopBtn("Search", "GJ_button_01.png", menu_selector(RightsBrowserPopup::onSearchButton), 282.f);
    addTopBtn("Monetize", "GJ_button_04.png", menu_selector(RightsBrowserPopup::onToggleCommercialOnly), 346.f);
    addTopBtn("Platform", "GJ_button_05.png", menu_selector(RightsBrowserPopup::onCyclePlatform), 406.f);

    m_filterStatusLabel = cocos2d::CCLabelBMFont::create("", "chatFont.fnt");
    m_filterStatusLabel->setScale(0.6f);
    m_filterStatusLabel->setPosition({sz.width / 2.f, sz.height - 76.f});
    m_mainLayer->addChild(m_filterStatusLabel);

    m_scrollLayer = geode::ScrollLayer::create({sz.width - 28.f, sz.height - 102.f});
    m_scrollLayer->setPosition({14.f, 14.f});
    m_mainLayer->addChild(m_scrollLayer);

    executeSearch();
    return true;
}

std::optional<Platform> RightsBrowserPopup::selectedPlatform() const noexcept {
    switch (m_platformCycleIndex) {
        case 1: return Platform::YouTube;
        case 2: return Platform::TikTok;
        case 3: return Platform::Twitch;
        case 4: return Platform::Instagram;
        case 5: return Platform::Facebook;
        default: return std::nullopt;
    }
}

void RightsBrowserPopup::executeSearch() {
    LocalSearchFilter filter;
    filter.queryText = m_searchInput ? std::string(m_searchInput->getString()) : "";
    filter.commercialAllowedOnly = m_commercialOnly;
    filter.requiredPlatformMonetizable = selectedPlatform();

    if (m_filterStatusLabel) {
        std::string status = "Filter: ";
        status += m_commercialOnly ? "[Commercial/Monetizable Only] " : "[All Statuses] ";
        if (auto plat = selectedPlatform()) {
            status += "| Platform: " + std::string(platformDisplayName(*plat));
        } else {
            status += "| Platform: All";
        }
        status += " (Searching...)";
        m_filterStatusLabel->setString(status.c_str());
    }

    net::BackendClient::get().searchSongs(
        m_searchHolder,
        std::move(filter),
        [this](std::vector<SongRightsRecord> records, bool offlineFallback) {
            m_results = std::move(records);
            m_lastSearchOffline = offlineFallback;
            rebuildResultsList();
        }
    );
}

void RightsBrowserPopup::rebuildResultsList() {
    if (!m_scrollLayer) {
        return;
    }

    if (m_filterStatusLabel) {
        std::string status = m_commercialOnly ? "Monetizable Only" : "All Statuses";
        if (auto plat = selectedPlatform()) {
            status += " | " + std::string(platformDisplayName(*plat));
        } else {
            status += " | All Platforms";
        }
        status += " | Results: " + std::to_string(m_results.size());
        if (m_lastSearchOffline) {
            status += " (Local Cache Only)";
        }
        m_filterStatusLabel->setString(status.c_str());
    }

    auto* content = m_scrollLayer->m_contentLayer;
    content->removeAllChildrenWithCleanup(true);

    const float rowHeight = 34.f;
    const float listWidth = m_scrollLayer->getContentSize().width;
    const float totalHeight = std::max(
        m_scrollLayer->getContentSize().height,
        static_cast<float>(m_results.size()) * rowHeight + 8.f
    );
    content->setContentSize({listWidth, totalHeight});

    for (std::size_t idx = 0; idx < m_results.size(); ++idx) {
        const auto& rec = m_results[idx];
        const float yPos = totalHeight - static_cast<float>(idx + 1) * rowHeight;

        auto* rowNode = cocos2d::CCNode::create();
        rowNode->setContentSize({listWidth, rowHeight - 2.f});
        rowNode->setPosition({0.f, yPos});

        const auto badge = badgeStyleForRecord(rec, false, m_lastSearchOffline);
        std::string summaryText = rec.title + " - " + rec.artist;
        if (summaryText.size() > 46) {
            summaryText = summaryText.substr(0, 43) + "...";
        }

        auto* titleLbl = cocos2d::CCLabelBMFont::create(summaryText.c_str(), "bigFont.fnt");
        titleLbl->setScale(0.36f);
        titleLbl->setAnchorPoint({0.f, 0.5f});
        titleLbl->setPosition({8.f, 20.f});
        rowNode->addChild(titleLbl);

        std::string metaLine = std::string(toApiString(rec.sourceType)) + " | " + rec.licenseName;
        if (metaLine.size() > 64) {
            metaLine = metaLine.substr(0, 61) + "...";
        }
        auto* subLbl = cocos2d::CCLabelBMFont::create(metaLine.c_str(), "chatFont.fnt");
        subLbl->setScale(0.52f);
        subLbl->setColor({185, 195, 205});
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setPosition({8.f, 8.f});
        rowNode->addChild(subLbl);

        auto* rowMenu = cocos2d::CCMenu::create();
        rowMenu->setPosition({0.f, 0.f});

        auto* inspectSpr = ButtonSprite::create(badge.shortTag.c_str(), "goldFont.fnt", "GJ_button_01.png", 0.5f);
        inspectSpr->setColor({badge.color.r, badge.color.g, badge.color.b});
        inspectSpr->setScale(0.72f);

        auto* inspectBtn = CCMenuItemSpriteExtra::create(
            inspectSpr,
            this,
            menu_selector(RightsBrowserPopup::onSelectResultRow)
        );
        inspectBtn->setTag(static_cast<int>(idx));
        inspectBtn->setPosition({listWidth - 46.f, 16.f});
        rowMenu->addChild(inspectBtn);

        rowNode->addChild(rowMenu);
        content->addChild(rowNode);
    }

    m_scrollLayer->moveToTop();
}

void RightsBrowserPopup::onSearchButton(cocos2d::CCObject*) {
    executeSearch();
}

void RightsBrowserPopup::onToggleCommercialOnly(cocos2d::CCObject*) {
    m_commercialOnly = !m_commercialOnly;
    executeSearch();
}

void RightsBrowserPopup::onCyclePlatform(cocos2d::CCObject*) {
    m_platformCycleIndex = (m_platformCycleIndex + 1) % 6;
    executeSearch();
}

void RightsBrowserPopup::onSelectResultRow(cocos2d::CCObject* sender) {
    auto* item = static_cast<cocos2d::CCMenuItem*>(sender);
    if (!item) return;
    const int idx = item->getTag();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_results.size()) {
        return;
    }

    const SongRightsRecord& rec = m_results[static_cast<std::size_t>(idx)];
    SongQuery q;
    q.sourceType = rec.sourceType;
    q.gdSongId = rec.gdSongId;
    q.title = rec.title;
    q.artist = rec.artist;
    q.sourceUrl = rec.originalSourceUrl;

    if (auto* detail = RightsDetailPopup::create(std::move(q), rec, m_lastSearchOffline)) {
        detail->show();
    }
}

}  // namespace mrc::ui
