#pragma once

#include <optional>
#include <string>

#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/async.hpp>

#include "../net/BackendClient.hpp"
#include <mrc/Types.hpp>

namespace mrc::ui {

class RightsDetailPopup : public geode::Popup {
public:
    static RightsDetailPopup* create(
        SongQuery query,
        std::optional<SongRightsRecord> initialRecord = std::nullopt,
        bool initialOfflineFallback = false
    );

protected:
    bool init(
        SongQuery query,
        std::optional<SongRightsRecord> initialRecord,
        bool initialOfflineFallback
    );

private:
    SongQuery m_query;
    std::optional<SongRightsRecord> m_record;
    bool m_offlineFallback = false;
    bool m_loading = false;
    std::string m_lastError;

    geode::MDTextArea* m_markdownArea = nullptr;
    cocos2d::CCLabelBMFont* m_statusSubtitle = nullptr;
    geode::async::TaskHolder<net::ClientResolveOutcome> m_resolveHolder;

    void triggerResolve(bool forceRefresh);
    void rebuildMarkdownContent();

    void onCopyAttribution(cocos2d::CCObject* sender);
    void onOpenSourceUrl(cocos2d::CCObject* sender);
    void onRefresh(cocos2d::CCObject* sender);
    void onOpenReportModal(cocos2d::CCObject* sender);
    void onOpenCatalogBrowser(cocos2d::CCObject* sender);
};

}  // namespace mrc::ui
