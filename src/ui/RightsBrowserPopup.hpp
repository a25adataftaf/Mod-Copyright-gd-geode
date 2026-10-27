#pragma once

#include <optional>
#include <string>
#include <vector>

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>

#include <mrc/LocalCache.hpp>
#include <mrc/Types.hpp>

namespace mrc::ui {

class RightsBrowserPopup : public geode::Popup {
public:
    static RightsBrowserPopup* create();

protected:
    bool init();

private:
    geode::TextInput* m_searchInput = nullptr;
    cocos2d::CCLabelBMFont* m_filterStatusLabel = nullptr;
    geode::ScrollLayer* m_scrollLayer = nullptr;

    bool m_commercialOnly = false;
    int m_platformCycleIndex = 0;  // 0 = Any, 1..5 = YouTube..Facebook
    bool m_lastSearchOffline = false;
    std::vector<SongRightsRecord> m_results;

    geode::async::TaskHolder<std::vector<SongRightsRecord>> m_searchHolder;

    [[nodiscard]] std::optional<Platform> selectedPlatform() const noexcept;
    void executeSearch();
    void rebuildResultsList();

    void onSearchButton(cocos2d::CCObject* sender);
    void onToggleCommercialOnly(cocos2d::CCObject* sender);
    void onCyclePlatform(cocos2d::CCObject* sender);
    void onSelectResultRow(cocos2d::CCObject* sender);
};

}  // namespace mrc::ui
