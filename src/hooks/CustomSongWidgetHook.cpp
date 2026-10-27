#include <optional>
#include <string>

#include <Geode/Geode.hpp>
#include <Geode/modify/CustomSongWidget.hpp>
#include <Geode/utils/async.hpp>

#include "../jukebox/JukeboxIntegration.hpp"
#include "../net/BackendClient.hpp"
#include "../ui/RightsDetailPopup.hpp"
#include <mrc/Types.hpp>

using namespace geode::prelude;

namespace {

/**
 * Hooks `CustomSongWidget` non-invasively to display a compact music rights badge
 * (`"rights-badge-button"_spr`) and automatically track active Jukebox NONG overrides.
 */
class $modify(MRCSongWidget, CustomSongWidget) {
    struct Fields {
        CCMenuItemSpriteExtra* badgeButton = nullptr;
        ButtonSprite* badgeSprite = nullptr;
        mrc::SongQuery activeQuery{};
        std::optional<mrc::SongRightsRecord> latestRecord{};
        bool offlineFallback = false;
        bool isLoading = false;
        async::TaskHolder<mrc::SongQuery> manifestTask{};
        async::TaskHolder<mrc::net::ClientResolveOutcome> networkTask{};
    };

    bool init(
        SongInfoObject* songInfo,
        CustomSongDelegate* songDelegate,
        bool showSongSelect,
        bool showPlayMusic,
        bool showDownload,
        bool isRobtopSong,
        bool hideBackground,
        bool isMusicLibrary,
        int unk
    ) {
        if (!CustomSongWidget::init(
                songInfo,
                songDelegate,
                showSongSelect,
                showPlayMusic,
                showDownload,
                isRobtopSong,
                hideBackground,
                isMusicLibrary,
                unk
            )) {
            return false;
        }

        if (!mrc::net::BackendClient::get().isModEnabled()) {
            return true;
        }
        if (this->m_isInCell && !Mod::get()->getSettingValue<bool>("show-in-compact-cells")) {
            return true;
        }

        ensureRightsBadgeCreated();
        refreshActiveSongAndRights(false);
        return true;
    }

    void updateSongInfo() {
        CustomSongWidget::updateSongInfo();
        if (!mrc::net::BackendClient::get().isModEnabled()) {
            return;
        }
        if (this->m_isInCell && !Mod::get()->getSettingValue<bool>("show-in-compact-cells")) {
            return;
        }
        ensureRightsBadgeCreated();
        refreshActiveSongAndRights(false);
    }

    void loadSongInfoFinished(SongInfoObject* info) {
        CustomSongWidget::loadSongInfoFinished(info);
        if (!mrc::net::BackendClient::get().isModEnabled()) {
            return;
        }
        if (this->m_isInCell && !Mod::get()->getSettingValue<bool>("show-in-compact-cells")) {
            return;
        }
        ensureRightsBadgeCreated();
        refreshActiveSongAndRights(false);
    }

    void ensureRightsBadgeCreated() {
        auto* fields = m_fields.self();
        if (fields->badgeButton != nullptr || this->m_buttonMenu == nullptr) {
            return;
        }

        auto* spr = ButtonSprite::create("MRC ?", "goldFont.fnt", "GJ_button_04.png", 0.5f);
        spr->setScale(this->m_isInCell ? 0.48f : 0.56f);

        auto* btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MRCSongWidget::onRightsBadgeClicked)
        );
        btn->setID("rights-badge-button"_spr);

        // Position in top-right corner of CustomSongWidget without overlapping Jukebox's top-left pin
        // or the bottom-right download/play buttons
        const CCSize widgetSize = this->getContentSize();
        const CCPoint menuPos = this->m_buttonMenu->getPosition();
        const CCPoint targetWorldLocal = {
            widgetSize.width / 2.f - 44.f - menuPos.x,
            widgetSize.height / 2.f - 16.f - menuPos.y
        };
        btn->setPosition(targetWorldLocal);

        this->m_buttonMenu->addChild(btn);
        fields->badgeSprite = spr;
        fields->badgeButton = btn;
    }

    void applyBadgeVisuals() {
        auto* fields = m_fields.self();
        if (!fields->badgeSprite) {
            return;
        }
        const auto style = mrc::badgeStyleForRecord(
            fields->latestRecord,
            fields->isLoading,
            fields->offlineFallback
        );
        fields->badgeSprite->setString(style.shortTag.c_str());
        fields->badgeSprite->setColor({style.color.r, style.color.g, style.color.b});
    }

    void refreshActiveSongAndRights(bool forceLiveCheck) {
        auto defaultQueryOpt = mrc::jb::JukeboxIntegration::extractDefaultSongFromWidget(this);
        if (!defaultQueryOpt.has_value()) {
            return;
        }

        auto* fields = m_fields.self();
        const int rawId = defaultQueryOpt->gdSongId.value_or(0);
        const int adjustedId = mrc::adjustJukeboxSongId(rawId, this->m_isRobtopSong);

        fields->manifestTask.spawn(
            mrc::jb::JukeboxIntegration::resolveActiveSongAsync(*defaultQueryOpt, adjustedId),
            [this, forceLiveCheck](mrc::SongQuery resolvedQuery) {
                auto* f = m_fields.self();
                f->activeQuery = std::move(resolvedQuery);

                if (!forceLiveCheck && !mrc::net::BackendClient::get().shouldAutoCheckSongs()) {
                    f->isLoading = false;
                    applyBadgeVisuals();
                    return;
                }

                f->isLoading = true;
                applyBadgeVisuals();

                mrc::net::BackendClient::get().resolveSongRights(
                    f->networkTask,
                    f->activeQuery,
                    forceLiveCheck,
                    [this](mrc::net::ClientResolveOutcome outcome) {
                        auto* inner = m_fields.self();
                        inner->isLoading = false;
                        inner->offlineFallback = outcome.servedFromOfflineFallback;
                        if (outcome.record.has_value()) {
                            inner->latestRecord = std::move(outcome.record);
                        }
                        applyBadgeVisuals();
                    }
                );
            }
        );
    }

    void onRightsBadgeClicked(CCObject*) {
        auto* fields = m_fields.self();
        if (fields->activeQuery.title.empty()) {
            if (auto defOpt = mrc::jb::JukeboxIntegration::extractDefaultSongFromWidget(this)) {
                fields->activeQuery = std::move(*defOpt);
            }
        }
        if (auto* popup = mrc::ui::RightsDetailPopup::create(
                fields->activeQuery,
                fields->latestRecord,
                fields->offlineFallback
            )) {
            popup->show();
        }
    }
};

}  // namespace
