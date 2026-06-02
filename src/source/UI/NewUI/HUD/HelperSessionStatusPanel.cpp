#include "stdafx.h"

#include <algorithm>
#include <cwchar>

#include "UI/NewUI/HUD/HelperSessionStatusPanel.h"

#include "Data/GameConfig/GameConfig.h"
#include "I18N/All.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Render/Textures/ZzzOpenglUtil.h"
#include "UI/NewUI/NewUICommon.h"
#include "UI/NewUI/NewUISystem.h"
#include "World/MapInfra/MapManager.h"

namespace UI::Helper::SessionStatus
{
    namespace
    {
        constexpr int WINDOW_WIDTH = Panel::kWindowWidth;
        constexpr int WINDOW_HEIGHT = Panel::kWindowHeight;
        constexpr int HEADER_HEIGHT = 18;
        constexpr int ROW_START_Y = 23;
        constexpr int ROW_HEIGHT = 11;
        constexpr int WINDOW_PADDING_X = 8;
        constexpr int CONTENT_WIDTH = WINDOW_WIDTH - WINDOW_PADDING_X * 2;
        constexpr int LABEL_WIDTH = 75;
        constexpr int VALUE_OFFSET_X = LABEL_WIDTH + 4;
        constexpr int CLOSE_SIZE = 12;
        constexpr int CLOSE_OFFSET_X = WINDOW_WIDTH - CLOSE_SIZE - 4;
        constexpr int CLOSE_OFFSET_Y = 3;
        constexpr int REFRESH_INTERVAL_MS = 250;
        constexpr float PANEL_DEPTH = 6.15f;
        constexpr float PANEL_KEY_ORDER = 2.0f;
        constexpr float BACK_ALPHA = 0.62f;
        constexpr float HEADER_ALPHA = 0.78f;
        constexpr int NUMBER_BUFFER_LENGTH = 40;

        void CopyWide(wchar_t* output, int outputLength, const wchar_t* value)
        {
            if (output == nullptr || outputLength <= 0)
            {
                return;
            }

            int index = 0;
            while (value != nullptr && value[index] != L'\0' && index < outputLength - 1)
            {
                output[index] = value[index];
                index++;
            }
            output[index] = L'\0';
        }

        void FormatTime(unsigned int totalSeconds, wchar_t* output, int outputLength)
        {
            constexpr unsigned int secondsPerMinute = 60;
            constexpr unsigned int secondsPerHour = 3600;
            const unsigned int hours = totalSeconds / secondsPerHour;
            const unsigned int minutes = (totalSeconds / secondsPerMinute) % secondsPerMinute;
            const unsigned int seconds = totalSeconds % secondsPerMinute;
            mu_swprintf_s(output, outputLength, L"%02u:%02u:%02u", hours, minutes, seconds);
        }

        void FormatInteger(unsigned long long value, wchar_t* output, int outputLength)
        {
            wchar_t raw[NUMBER_BUFFER_LENGTH] = {};
            mu_swprintf_s(raw, NUMBER_BUFFER_LENGTH, L"%llu", value);
            const int rawLength = static_cast<int>(wcsnlen(raw, NUMBER_BUFFER_LENGTH));
            const int commaCount = rawLength > 0 ? (rawLength - 1) / 3 : 0;
            int readIndex = rawLength - 1;
            int writeIndex = std::min(rawLength + commaCount, outputLength - 1);
            output[writeIndex--] = L'\0';

            for (int digits = 0; readIndex >= 0 && writeIndex >= 0; digits++)
            {
                if (digits > 0 && digits % 3 == 0)
                {
                    output[writeIndex--] = L',';
                }
                output[writeIndex--] = raw[readIndex--];
            }
        }

        void FormatCentiseconds(unsigned int value100, wchar_t* output, int outputLength)
        {
            mu_swprintf_s(output, outputLength, L"%u.%02us", value100 / 100, value100 % 100);
        }
    }

    Panel::Panel()
    {
        m_NewUIManager = nullptr;
        m_Pos.x = 0;
        m_Pos.y = 0;
        m_IsDragging = false;
        m_DragOffsetX = 0;
        m_DragOffsetY = 0;
        m_LastRefreshTick = 0;
        memset(m_Text, 0, sizeof(m_Text));
        memset(m_Values, 0, sizeof(m_Values));
    }

    Panel::~Panel()
    {
        Release();
    }

    bool Panel::Create(SEASON3B::CNewUIManager* newUIManager, int x, int y)
    {
        if (newUIManager == nullptr)
        {
            return false;
        }

        m_NewUIManager = newUIManager;
        m_NewUIManager->AddUIObj(SEASON3B::INTERFACE_HELPER_SESSION_STATUS, this);

        const int savedX = GameConfig::GetInstance().GetHelperSessionPanelX();
        const int savedY = GameConfig::GetInstance().GetHelperSessionPanelY();
        if (savedX >= 0 && savedY >= 0)
        {
            SetPos(savedX, savedY);
        }
        else
        {
            SetPos(x, y);
        }

        LoadText();
        Show(false);
        return true;
    }

    void Panel::Release()
    {
        if (m_NewUIManager == nullptr)
        {
            return;
        }

        m_NewUIManager->RemoveUIObj(this);
        m_NewUIManager = nullptr;
    }

    void Panel::SetPos(int x, int y)
    {
        m_Pos.x = x;
        m_Pos.y = y;
        ClampToScreen();
    }

    void Panel::LoadText()
    {
        LoadMetricText();
        LoadStateText();
        for (int i = 0; i < METRIC_COUNT; i++)
        {
            CopyWide(m_Values[i], VALUE_LENGTH, L"");
        }
    }

    void Panel::LoadMetricText()
    {
        struct TextSpec { TextId id; const wchar_t* text; };
        static const TextSpec specs[] =
        {
            { TextId::Title, I18N::Game::HelperSessionStatusTitle },
            { TextId::SessionTime, I18N::Game::HelperSessionStatusSessionTime },
            { TextId::CurrentSpot, I18N::Game::HelperSessionStatusCurrentSpot },
            { TextId::TotalDamage, I18N::Game::HelperSessionStatusTotalDamage },
            { TextId::AverageDps, I18N::Game::HelperSessionStatusAverageDps },
            { TextId::Kills, I18N::Game::HelperSessionStatusKills },
            { TextId::AverageKillTime, I18N::Game::HelperSessionStatusAverageKillTime },
            { TextId::ExpGained, I18N::Game::HelperSessionStatusExpGained },
            { TextId::ExpPerHour, I18N::Game::HelperSessionStatusExpPerHour },
            { TextId::ZenPicked, I18N::Game::HelperSessionStatusZenPicked },
            { TextId::ZenPerHour, I18N::Game::HelperSessionStatusZenPerHour },
            { TextId::JewelsPicked, I18N::Game::HelperSessionStatusJewelsPicked },
            { TextId::HpPotionsUsed, I18N::Game::HelperSessionStatusHpPotionsUsed },
            { TextId::MpPotionsUsed, I18N::Game::HelperSessionStatusMpPotionsUsed },
            { TextId::IdleTime, I18N::Game::HelperSessionStatusIdleTime },
            { TextId::StuckTime, I18N::Game::HelperSessionStatusStuckTime },
            { TextId::LastDeathReason, I18N::Game::HelperSessionStatusLastDeathReason },
        };

        for (const TextSpec& spec : specs)
        {
            CopyWide(m_Text[static_cast<int>(spec.id)], TEXT_LENGTH, spec.text);
        }
    }

    void Panel::LoadStateText()
    {
        struct TextSpec { TextId id; const wchar_t* text; };
        static const TextSpec specs[] =
        {
            { TextId::NotAvailable, I18N::Game::HelperSessionStatusNotAvailable },
            { TextId::Unknown, I18N::Game::HelperSessionStatusUnknown },
            { TextId::None, I18N::Game::HelperSessionStatusNone },
            { TextId::Disconnected, I18N::Game::HelperSessionStatusDisconnected },
            { TextId::KilledByMonster, I18N::Game::HelperSessionStatusKilledByMonster },
            { TextId::KilledByPK, I18N::Game::HelperSessionStatusKilledByPK },
            { TextId::Pieces, I18N::Game::HelperSessionStatusPieces },
        };

        for (const TextSpec& spec : specs)
        {
            CopyWide(m_Text[static_cast<int>(spec.id)], TEXT_LENGTH, spec.text);
        }
    }

    bool Panel::UpdateMouseEvent()
    {
        if (!IsVisible())
        {
            return true;
        }

        if (HandleCloseButton())
        {
            return false;
        }

        if (HandleDragging())
        {
            return false;
        }

        return !SEASON3B::CheckMouseIn(m_Pos.x, m_Pos.y, WINDOW_WIDTH, WINDOW_HEIGHT);
    }

    bool Panel::UpdateKeyEvent()
    {
        return true;
    }

    bool Panel::Update()
    {
        if (!IsVisible())
        {
            return true;
        }

        const DWORD currentTick = GetTickCount();
        if (currentTick - m_LastRefreshTick >= REFRESH_INTERVAL_MS)
        {
            m_LastRefreshTick = currentTick;
            RefreshValues();
        }
        return true;
    }

    bool Panel::Render()
    {
        if (!IsVisible())
        {
            return true;
        }

        if (!m_Snapshot.hasSession)
        {
            return true;
        }

        RenderBackground();
        RenderHeader();
        RenderRows();
        DisableAlphaBlend();
        return true;
    }

    float Panel::GetLayerDepth()
    {
        return PANEL_DEPTH;
    }

    float Panel::GetKeyEventOrder()
    {
        return PANEL_KEY_ORDER;
    }

    void Panel::RefreshValues()
    {
        wchar_t value[VALUE_LENGTH] = {};
        GameLogic::Helper::SessionStats::GetSnapshot(m_Snapshot);

        FormatTime(m_Snapshot.activeSeconds, value, VALUE_LENGTH);
        SetValue(MetricId::SessionTime, value);
        FormatCurrentSpot(value, VALUE_LENGTH);
        SetValue(MetricId::CurrentSpot, value);
        RefreshDamageValues(value, VALUE_LENGTH);
        RefreshKillEconomyValues(value, VALUE_LENGTH);
        RefreshPotionStateValues(value, VALUE_LENGTH);
    }

    void Panel::RefreshDamageValues(wchar_t* value, int valueLength)
    {
        FormatInteger(m_Snapshot.totalDamage, value, valueLength);
        SetValue(MetricId::TotalDamage, value);
        FormatInteger(m_Snapshot.averageDps, value, valueLength);
        SetValue(MetricId::AverageDps, value);
        FormatInteger(m_Snapshot.kills, value, valueLength);
        SetValue(MetricId::Kills, value);
    }

    void Panel::RefreshKillEconomyValues(wchar_t* value, int valueLength)
    {
        m_Snapshot.kills > 0 ? FormatCentiseconds(m_Snapshot.averageKillTimeCentiseconds, value, valueLength) : CopyWide(value, valueLength, GetText(TextId::NotAvailable));
        SetValue(MetricId::AverageKillTime, value);
        FormatInteger(m_Snapshot.experienceGained, value, valueLength);
        SetValue(MetricId::ExpGained, value);
        FormatInteger(m_Snapshot.experiencePerHour, value, valueLength);
        SetValue(MetricId::ExpPerHour, value);
        FormatInteger(m_Snapshot.zenPicked, value, valueLength);
        SetValue(MetricId::ZenPicked, value);
        FormatInteger(m_Snapshot.zenPerHour, value, valueLength);
        SetValue(MetricId::ZenPerHour, value);
        mu_swprintf_s(value, valueLength, L"%u %ls", m_Snapshot.jewelsPicked, GetText(TextId::Pieces));
        SetValue(MetricId::JewelsPicked, value);
    }

    void Panel::RefreshPotionStateValues(wchar_t* value, int valueLength)
    {
        FormatInteger(m_Snapshot.hpPotionsUsed, value, valueLength);
        SetValue(MetricId::HpPotionsUsed, value);
        FormatInteger(m_Snapshot.mpPotionsUsed, value, valueLength);
        SetValue(MetricId::MpPotionsUsed, value);
        FormatTime(m_Snapshot.idleSeconds, value, valueLength);
        SetValue(MetricId::IdleTime, value);
        FormatTime(m_Snapshot.stuckSeconds, value, valueLength);
        SetValue(MetricId::StuckTime, value);
        FormatDeathReason(m_Snapshot.lastDeathReason, value, valueLength);
        SetValue(MetricId::LastDeathReason, value);
    }

    void Panel::RenderBackground() const
    {
        EnableAlphaTest();
        RenderColor(static_cast<float>(m_Pos.x), static_cast<float>(m_Pos.y), WINDOW_WIDTH, WINDOW_HEIGHT, BACK_ALPHA, 1);
        EndRenderColor();
        RenderColor(static_cast<float>(m_Pos.x), static_cast<float>(m_Pos.y), WINDOW_WIDTH, HEADER_HEIGHT, HEADER_ALPHA, 1);
        EndRenderColor();
    }

    void Panel::RenderHeader() const
    {
        g_pRenderText->SetFont(g_hFontBold);
        g_pRenderText->SetTextColor(255, 255, 255, 255);
        g_pRenderText->SetBgColor(0, 0, 0, 0);
        g_pRenderText->RenderText(m_Pos.x + WINDOW_PADDING_X, m_Pos.y + 4, GetText(TextId::Title), WINDOW_WIDTH - 32, 0, RT3_SORT_LEFT);
        g_pRenderText->RenderText(m_Pos.x + CLOSE_OFFSET_X, m_Pos.y + 2, L"x", CLOSE_SIZE, 0, RT3_SORT_CENTER);
    }

    void Panel::RenderRows() const
    {
        g_pRenderText->SetFont(g_hFont);
        for (int row = 0; row < METRIC_COUNT; row++)
        {
            const int y = m_Pos.y + ROW_START_Y + row * ROW_HEIGHT;
            RenderMetric(row, m_Pos.x + WINDOW_PADDING_X, y, CONTENT_WIDTH);
        }
    }

    void Panel::RenderMetric(int metricIndex, int x, int y, int width) const
    {
        const MetricId metricId = static_cast<MetricId>(metricIndex);
        if (metricIndex < 0 || metricIndex >= METRIC_COUNT)
        {
            return;
        }

        g_pRenderText->SetTextColor(190, 205, 215, 230);
        g_pRenderText->RenderText(x, y, GetText(GetMetricLabel(metricId)), LABEL_WIDTH, 0, RT3_SORT_LEFT);
        g_pRenderText->SetTextColor(255, 255, 255, 255);
        g_pRenderText->RenderText(x + VALUE_OFFSET_X, y, m_Values[metricIndex], width - VALUE_OFFSET_X, 0, RT3_SORT_RIGHT);
    }

    bool Panel::HandleCloseButton()
    {
        const bool closeHover = SEASON3B::CheckMouseIn(m_Pos.x + CLOSE_OFFSET_X, m_Pos.y + CLOSE_OFFSET_Y, CLOSE_SIZE, CLOSE_SIZE);
        if (!closeHover || !MouseLButtonPush)
        {
            return false;
        }

        g_pNewUISystem->Hide(SEASON3B::INTERFACE_HELPER_SESSION_STATUS);
        m_IsDragging = false;
        return true;
    }

    bool Panel::HandleDragging()
    {
        const bool wasDragging = m_IsDragging;
        if (MouseLButtonPop)
        {
            m_IsDragging = false;
        }

        if (!m_IsDragging && MouseLButtonPush && SEASON3B::CheckMouseIn(m_Pos.x, m_Pos.y, WINDOW_WIDTH, HEADER_HEIGHT))
        {
            m_IsDragging = true;
            m_DragOffsetX = MouseX - m_Pos.x;
            m_DragOffsetY = MouseY - m_Pos.y;
        }

        if (!m_IsDragging)
        {
            // Persist the final position once the user releases the drag.
            if (wasDragging)
            {
                GameConfig::GetInstance().SetHelperSessionPanelPosition(m_Pos.x, m_Pos.y);
            }
            return false;
        }

        SetPos(MouseX - m_DragOffsetX, MouseY - m_DragOffsetY);
        return true;
    }

    void Panel::ClampToScreen()
    {
        m_Pos.x = std::clamp(static_cast<int>(m_Pos.x), 0, REFERENCE_WIDTH - WINDOW_WIDTH);
        m_Pos.y = std::clamp(static_cast<int>(m_Pos.y), 0, REFERENCE_HEIGHT - WINDOW_HEIGHT);
    }

    void Panel::SetValue(MetricId metricId, const wchar_t* value)
    {
        const int index = static_cast<int>(metricId);
        if (index < 0 || index >= METRIC_COUNT)
        {
            return;
        }

        CopyWide(m_Values[index], VALUE_LENGTH, value);
    }

    const wchar_t* Panel::GetText(TextId textId) const
    {
        const int index = static_cast<int>(textId);
        if (index < 0 || index >= TEXT_COUNT)
        {
            return L"";
        }

        return m_Text[index];
    }

    Panel::TextId Panel::GetMetricLabel(MetricId metricId) const
    {
        static constexpr TextId labels[METRIC_COUNT] =
        {
            TextId::SessionTime, TextId::CurrentSpot, TextId::TotalDamage, TextId::AverageDps,
            TextId::Kills, TextId::AverageKillTime, TextId::ExpGained, TextId::ExpPerHour,
            TextId::ZenPicked, TextId::ZenPerHour, TextId::JewelsPicked, TextId::HpPotionsUsed,
            TextId::MpPotionsUsed, TextId::IdleTime, TextId::StuckTime, TextId::LastDeathReason,
        };

        const int index = static_cast<int>(metricId);
        if (index < 0 || index >= METRIC_COUNT)
        {
            return TextId::Count;
        }

        return labels[index];
    }

    void Panel::FormatCurrentSpot(wchar_t* output, int outputLength) const
    {
        if (gMapManager.WorldActive < 0)
        {
            CopyWide(output, outputLength, GetText(TextId::Unknown));
            return;
        }

        if (Hero == nullptr)
        {
            CopyWide(output, outputLength, gMapManager.GetMapName(gMapManager.WorldActive));
            return;
        }

        mu_swprintf_s(output, outputLength, L"%ls (%d, %d)",
            gMapManager.GetMapName(gMapManager.WorldActive),
            Hero->PositionX,
            Hero->PositionY);
    }

    void Panel::FormatDeathReason(GameLogic::Helper::SessionStats::DeathReason reason, wchar_t* output, int outputLength) const
    {
        using GameLogic::Helper::SessionStats::DeathReason;
        switch (reason)
        {
        case DeathReason::None: CopyWide(output, outputLength, GetText(TextId::None)); return;
        case DeathReason::KilledByMonster: CopyWide(output, outputLength, GetText(TextId::KilledByMonster)); return;
        case DeathReason::KilledByPK: CopyWide(output, outputLength, GetText(TextId::KilledByPK)); return;
        case DeathReason::Disconnected: CopyWide(output, outputLength, GetText(TextId::Disconnected)); return;
        default: CopyWide(output, outputLength, GetText(TextId::Unknown)); return;
        }
    }
}
