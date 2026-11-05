#include "RightsDetailPopup.hpp"
#include "RightsBrowserPopup.hpp"

#include <sstream>

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/web.hpp>

#include <mrc/AttributionAndSafety.hpp>

namespace mrc::ui {
namespace {

/**
 * Escapes untrusted external metadata before rendering inside `geode::MDTextArea`
 * so untrusted song titles or evidence excerpts cannot inject markdown links
 * (including Geode `mod:`, `user:`, or `level:` pseudo-links) or formatting tags.
 */
std::string escapeMarkdownUntrusted(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '\\':
            case '*':
            case '_':
            case '[':
            case ']':
            case '(':
            case ')':
            case '<':
            case '>':
            case '`':
            case '#':
                out.push_back('\\');
                out.push_back(c);
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string formatPermColorTag(PermissionLevel p) {
    switch (p) {
        case PermissionLevel::Allowed:
            return "<cg>Permitted</c>";
        case PermissionLevel::Conditional:
            return "<cy>Conditional</c>";
        case PermissionLevel::ConditionalRevenueShare:
            return "<cy>Revenue Share</c>";
        case PermissionLevel::NotAllowed:
            return "<cr>Not Permitted</c>";
        case PermissionLevel::Unknown:
            return "<cj>Unverified</c>";
    }
    return "Unverified";
}

std::string formatClaimRiskTag(ClaimRisk r) {
    switch (r) {
        case ClaimRisk::Low:
            return "<cg>Low</c>";
        case ClaimRisk::Moderate:
            return "<cy>Moderate</c>";
        case ClaimRisk::High:
            return "<cr>High</c>";
        case ClaimRisk::Unknown:
            return "<cj>Unknown</c>";
    }
    return "Unknown";
}

class ReportIssuePopup : public geode::Popup {
public:
    static ReportIssuePopup* create(std::string songId, std::string songTitle) {
        auto* ret = new ReportIssuePopup();
        if (ret && ret->initPopup(std::move(songId), std::move(songTitle))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

private:
    std::string m_songId;
    geode::TextInput* m_reasonInput = nullptr;
    geode::TextInput* m_detailsInput = nullptr;
    geode::TextInput* m_urlInput = nullptr;
    geode::async::TaskHolder<geode::Result<std::string>> m_submitHolder;

    bool initPopup(std::string songId, std::string songTitle) {
        if (!Popup::init(360.f, 225.f)) {
            return false;
        }
        m_songId = std::move(songId);
        this->setTitle("Report Rights Inaccuracy");

        const auto contentSize = m_mainLayer->getContentSize();

        auto* sub = cocos2d::CCLabelBMFont::create(
            ("Song: " + songTitle.substr(0, 42)).c_str(),
            "chatFont.fnt"
        );
        sub->setScale(0.65f);
        sub->setPosition({contentSize.width / 2.f, contentSize.height - 42.f});
        m_mainLayer->addChild(sub);

        m_reasonInput = geode::TextInput::create(310.f, "Short reason (e.g. Artist granted CC-BY 4.0)");
        m_reasonInput->setCommonFilter(geode::CommonFilter::Any);
        m_reasonInput->setMaxCharCount(180);
        m_reasonInput->setPosition({contentSize.width / 2.f, contentSize.height - 78.f});
        m_mainLayer->addChild(m_reasonInput);

        m_detailsInput = geode::TextInput::create(310.f, "Supporting details or licence explanation");
        m_detailsInput->setCommonFilter(geode::CommonFilter::Any);
        m_detailsInput->setMaxCharCount(500);
        m_detailsInput->setPosition({contentSize.width / 2.f, contentSize.height - 118.f});
        m_mainLayer->addChild(m_detailsInput);

        m_urlInput = geode::TextInput::create(310.f, "Optional official URL (https://...)");
        m_urlInput->setCommonFilter(geode::CommonFilter::Any);
        m_urlInput->setMaxCharCount(300);
        m_urlInput->setPosition({contentSize.width / 2.f, contentSize.height - 158.f});
        m_mainLayer->addChild(m_urlInput);

        auto* submitSpr = ButtonSprite::create("Submit Report", "goldFont.fnt", "GJ_button_01.png", 0.7f);
        auto* submitBtn = CCMenuItemSpriteExtra::create(
            submitSpr,
            this,
            menu_selector(ReportIssuePopup::onSubmit)
        );
        m_buttonMenu->addChildAtPosition(submitBtn, geode::Anchor::Bottom, {0.f, 24.f});

        return true;
    }

    void onSubmit(cocos2d::CCObject*) {
        const std::string reason = m_reasonInput->getString();
        const std::string details = m_detailsInput->getString();
        const std::string evUrl = m_urlInput->getString();

        if (reason.size() < 4 || details.size() < 8) {
            geode::Notification::create(
                "Please enter both a reason and brief supporting details.",
                geode::NotificationIcon::Warning
            )->show();
            return;
        }
        if (!evUrl.empty() && !isSafeBrowserUrl(evUrl)) {
            geode::Notification::create(
                "Evidence URL must be a valid public https:// link.",
                geode::NotificationIcon::Warning
            )->show();
            return;
        }

        net::BackendClient::get().submitReviewReport(
            m_submitHolder,
            m_songId,
            reason,
            details,
            evUrl,
            [this](geode::Result<std::string> res) {
                if (res.isOk()) {
                    geode::Notification::create(
                        res.unwrap(),
                        geode::NotificationIcon::Success
                    )->show();
                    this->onClose(nullptr);
                } else {
                    geode::Notification::create(
                        res.unwrapErr(),
                        geode::NotificationIcon::Error
                    )->show();
                }
            }
        );
    }
};

}  // namespace

RightsDetailPopup* RightsDetailPopup::create(
    SongQuery query,
    std::optional<SongRightsRecord> initialRecord,
    bool initialOfflineFallback
) {
    auto* ret = new RightsDetailPopup();
    if (ret && ret->init(std::move(query), std::move(initialRecord), initialOfflineFallback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RightsDetailPopup::init(
    SongQuery query,
    std::optional<SongRightsRecord> initialRecord,
    bool initialOfflineFallback
) {
    if (!Popup::init(450.f, 290.f)) {
        return false;
    }

    m_query = std::move(query);
    m_record = std::move(initialRecord);
    m_offlineFallback = initialOfflineFallback;

    this->setTitle("Music Rights Checker");
    const auto sz = m_mainLayer->getContentSize();

    m_statusSubtitle = cocos2d::CCLabelBMFont::create("", "chatFont.fnt");
    m_statusSubtitle->setScale(0.62f);
    m_statusSubtitle->setPosition({sz.width / 2.f, sz.height - 38.f});
    m_mainLayer->addChild(m_statusSubtitle);

    m_markdownArea = geode::MDTextArea::create(
        "Loading song rights evaluation...",
        {sz.width - 28.f, sz.height - 96.f}
    );
    m_markdownArea->setPosition({sz.width / 2.f, sz.height / 2.f - 4.f});
    m_mainLayer->addChild(m_markdownArea);

    auto makeBottomButton = [&](const char* label, const char* bgTex, cocos2d::SEL_MenuHandler sel, float xOffset) {
        auto* spr = ButtonSprite::create(label, "goldFont.fnt", bgTex, 0.58f);
        spr->setScale(0.78f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setID(std::string("mrc-btn-") + label);
        m_buttonMenu->addChildAtPosition(btn, geode::Anchor::Bottom, {xOffset, 22.f});
    };

    makeBottomButton("Copy Credit", "GJ_button_01.png", menu_selector(RightsDetailPopup::onCopyAttribution), -156.f);
    makeBottomButton("Source Page", "GJ_button_04.png", menu_selector(RightsDetailPopup::onOpenSourceUrl), -74.f);
    makeBottomButton("Refresh", "GJ_button_02.png", menu_selector(RightsDetailPopup::onRefresh), 4.f);
    makeBottomButton("Report Issue", "GJ_button_05.png", menu_selector(RightsDetailPopup::onOpenReportModal), 82.f);
    makeBottomButton("Catalog", "GJ_button_04.png", menu_selector(RightsDetailPopup::onOpenCatalogBrowser), 162.f);

    rebuildMarkdownContent();

    if (!m_record.has_value() || m_record->isStale) {
        triggerResolve(false);
    }

    return true;
}

void RightsDetailPopup::triggerResolve(bool forceRefresh) {
    m_loading = true;
    m_lastError.clear();
    rebuildMarkdownContent();

    net::BackendClient::get().resolveSongRights(
        m_resolveHolder,
        m_query,
        forceRefresh,
        [this](net::ClientResolveOutcome outcome) {
            m_loading = false;
            m_offlineFallback = outcome.servedFromOfflineFallback;
            if (outcome.record.has_value()) {
                m_record = std::move(outcome.record);
            }
            if (!outcome.errorMessage.empty()) {
                m_lastError = std::move(outcome.errorMessage);
            }
            rebuildMarkdownContent();
        }
    );
}

void RightsDetailPopup::rebuildMarkdownContent() {
    const auto badge = badgeStyleForRecord(m_record, m_loading, m_offlineFallback);
    if (m_statusSubtitle) {
        m_statusSubtitle->setString((badge.headline + " - " + badge.subline).c_str());
        m_statusSubtitle->setColor({badge.color.r, badge.color.g, badge.color.b});
    }

    std::ostringstream md;

    if (m_loading && !m_record.has_value()) {
        md << "### Investigating Song Rights...\n\n"
           << "- **Title:** " << escapeMarkdownUntrusted(m_query.title) << "\n"
           << "- **Artist:** " << escapeMarkdownUntrusted(m_query.artist) << "\n"
           << "- **Source Category:** `" << toApiString(m_query.sourceType) << "`\n\n"
           << "Querying centralized verification service without blocking gameplay...";
        m_markdownArea->setString(md.str().c_str());
        return;
    }

    if (!m_record.has_value()) {
        md << "### <cr>Rights Data Unavailable</c>\n\n"
           << "- **Title:** " << escapeMarkdownUntrusted(m_query.title) << "\n"
           << "- **Artist:** " << escapeMarkdownUntrusted(m_query.artist) << "\n"
           << "- **Source Category:** `" << toApiString(m_query.sourceType) << "`\n\n";
        if (!m_lastError.empty()) {
            md << "**Network / Backend Error:** " << escapeMarkdownUntrusted(m_lastError) << "\n\n";
        }
        md << "**Conservative Guidance:** Treat this song as **Unverified / Do Not Monetize** until "
              "authoritative licence evidence is retrieved.";
        m_markdownArea->setString(md.str().c_str());
        return;
    }

    const SongRightsRecord& rec = *m_record;

    if (rec.isStale || m_offlineFallback) {
        md << "<cy>**[CACHED / STALE RECORD]**</c> Shown from local cache because live verification was "
              "skipped, expired, or offline. Do not treat stale records as freshly verified.\n";
        if (!m_lastError.empty()) {
            md << "- *Note:* " << escapeMarkdownUntrusted(m_lastError) << "\n";
        }
        md << "\n";
    }

    md << "### " << escapeMarkdownUntrusted(rec.title) << " - " << escapeMarkdownUntrusted(rec.artist) << "\n";
    if (!rec.originalArtist.empty() && rec.originalArtist != rec.artist) {
        md << "- **Original Artist:** " << escapeMarkdownUntrusted(rec.originalArtist) << "\n";
    }
    md << "- **Detected Source:** `" << toApiString(rec.sourceType) << "`";
    if (rec.gdSongId.has_value()) {
        md << " (GD ID: `" << *rec.gdSongId << "`)";
    }
    md << "\n";

    md << "- **Copyright Ownership:** " << escapeMarkdownUntrusted(rec.copyrightOwnershipStatus) << "\n";
    if (!rec.rightsHolders.empty()) {
        md << "- **Rights Holders:** ";
        for (std::size_t i = 0; i < rec.rightsHolders.size(); ++i) {
            if (i > 0) md << ", ";
            md << escapeMarkdownUntrusted(rec.rightsHolders[i]);
        }
        md << "\n";
    }
    md << "- **Applicable Licence:** " << escapeMarkdownUntrusted(rec.licenseName) << "\n";
    md << "- **Overall Status:** `" << toApiString(rec.overallStatus) << "` | **Commercial Use:** "
       << formatPermColorTag(rec.commercialUseStatus) << "\n";
    md << "- **Attribution Required:** " << (rec.attributionRequired ? "<cy>Yes</c>" : "No / See Platform Rules")
       << "\n";
    md << "- **Last Verified (UTC):** `"
       << (rec.lastVerifiedAtIso.empty() ? "unknown" : escapeMarkdownUntrusted(rec.lastVerifiedAtIso))
       << "` via `" << escapeMarkdownUntrusted(rec.verificationMethod) << "`\n\n";

    md << "---\n#### Platform-Specific Permissions\n";
    md << "*Permission on one platform NEVER guarantees permission on another.*\n\n";

    for (const auto& pr : rec.platforms) {
        md << "**" << platformDisplayName(pr.platform) << "**\n"
           << "- Upload: " << formatPermColorTag(pr.videoUpload)
           << " | Stream: " << formatPermColorTag(pr.livestream)
           << " | Monetize: " << formatPermColorTag(pr.monetization)
           << " | Claim Risk: " << formatClaimRiskTag(pr.automatedClaimRisk) << "\n";
        for (const auto& cond : pr.conditions) {
            md << "  - *Condition:* " << escapeMarkdownUntrusted(cond) << "\n";
        }
        for (const auto& rest : pr.restrictions) {
            md << "  - <cr>*Restriction:*</c> " << escapeMarkdownUntrusted(rest) << "\n";
        }
    }

    if (!rec.conflictingInformation.empty()) {
        md << "\n---\n#### <cr>Conflicting Information</c>\n";
        for (const auto& conflict : rec.conflictingInformation) {
            md << "- " << escapeMarkdownUntrusted(conflict) << "\n";
        }
    }

    if (!rec.uncertainties.empty()) {
        md << "\n---\n#### <cy>Uncertainties & Limitations</c>\n";
        for (const auto& unc : rec.uncertainties) {
            md << "- " << escapeMarkdownUntrusted(unc) << "\n";
        }
    }

    if (!rec.evidence.empty()) {
        md << "\n---\n#### Supporting Evidence\n";
        for (const auto& ev : rec.evidence) {
            md << "- **" << escapeMarkdownUntrusted(ev.sourceTitle) << "** (`"
               << escapeMarkdownUntrusted(ev.fetchStatus) << "`): "
               << escapeMarkdownUntrusted(ev.excerpt) << "\n";
        }
    }

    if (!rec.attributionTemplate.empty()) {
        md << "\n---\n#### Copy-Ready Attribution\n```\n"
           << escapeMarkdownUntrusted(rec.attributionTemplate) << "\n```\n";
    }

    md << "\n---\n*Disclaimer: "
       << escapeMarkdownUntrusted(
              rec.disclaimer.empty()
                  ? "Informational guidance only, not legal advice. Automated Content ID / DMCA claims can occur regardless of licence status."
                  : rec.disclaimer
          )
       << "*";

    m_markdownArea->setString(md.str().c_str());
}

void RightsDetailPopup::onCopyAttribution(cocos2d::CCObject*) {
    if (!m_record.has_value()) {
        geode::Notification::create(
            "No verified rights record available to generate attribution.",
            geode::NotificationIcon::Warning
        )->show();
        return;
    }

    const std::string credit = formatAttributionForCopy(*m_record);
    if (credit.empty()) {
        geode::Notification::create(
            "Song is restricted or unverified; attribution alone does NOT grant permission.",
            geode::NotificationIcon::Warning
        )->show();
        return;
    }

    if (geode::utils::clipboard::write(credit)) {
        geode::Notification::create(
            "Attribution copied to clipboard!",
            geode::NotificationIcon::Success
        )->show();
    } else {
        geode::Notification::create(
            "Failed to access system clipboard.",
            geode::NotificationIcon::Error
        )->show();
    }
}

void RightsDetailPopup::onOpenSourceUrl(cocos2d::CCObject*) {
    std::string targetUrl;
    if (m_record.has_value() && !m_record->originalSourceUrl.empty()) {
        targetUrl = m_record->originalSourceUrl;
    } else if (!m_query.sourceUrl.empty()) {
        targetUrl = m_query.sourceUrl;
    } else if (m_record.has_value() && !m_record->licenseUrl.empty()) {
        targetUrl = m_record->licenseUrl;
    }

    if (targetUrl.empty() || !isSafeBrowserUrl(targetUrl)) {
        geode::Notification::create(
            "No verified public https:// source URL is available for this track.",
            geode::NotificationIcon::Warning
        )->show();
        return;
    }

    geode::utils::web::openLinkInBrowser(targetUrl);
}

void RightsDetailPopup::onRefresh(cocos2d::CCObject*) {
    triggerResolve(true);
}

void RightsDetailPopup::onOpenReportModal(cocos2d::CCObject*) {
    if (!m_record.has_value() || m_record->id.empty()) {
        geode::Notification::create(
            "Resolve the song first before submitting a report.",
            geode::NotificationIcon::Warning
        )->show();
        return;
    }
    if (m_record->id.starts_with("direct:")) {
        geode::utils::web::openLinkInBrowser(
            "https://github.com/a25adataftaf/Mod-Copyright-gd-geode/issues"
        );
        return;
    }
    if (auto* modal = ReportIssuePopup::create(m_record->id, m_record->title)) {
        modal->show();
    }
}

void RightsDetailPopup::onOpenCatalogBrowser(cocos2d::CCObject*) {
    if (auto* browser = RightsBrowserPopup::create()) {
        browser->show();
    }
}

}  // namespace mrc::ui
