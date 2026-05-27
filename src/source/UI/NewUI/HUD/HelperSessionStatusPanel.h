#pragma once

#include "GameLogic/Helper/SessionStats.h"
#include "UI/NewUI/NewUIBase.h"
#include "UI/NewUI/NewUIManager.h"

namespace UI::Helper::SessionStatus
{
    class Panel : public SEASON3B::CNewUIObj
    {
    public:
        static constexpr int kWindowWidth = 140;
        static constexpr int kWindowHeight = 203;

        Panel();
        ~Panel() override;

        bool Create(SEASON3B::CNewUIManager* newUIManager, int x, int y);
        void Release();
        void SetPos(int x, int y);

        bool UpdateMouseEvent() override;
        bool UpdateKeyEvent() override;
        bool Update() override;
        bool Render() override;

        float GetLayerDepth() override;
        float GetKeyEventOrder() override;

    private:
        enum class TextId
        {
            Title,
            SessionTime,
            CurrentSpot,
            TotalDamage,
            AverageDps,
            Kills,
            AverageKillTime,
            ExpGained,
            ExpPerHour,
            ZenPicked,
            ZenPerHour,
            JewelsPicked,
            HpPotionsUsed,
            MpPotionsUsed,
            IdleTime,
            StuckTime,
            LastDeathReason,
            NotAvailable,
            Unknown,
            None,
            Disconnected,
            KilledByMonster,
            KilledByPK,
            Pieces,
            Count,
        };

        enum class MetricId
        {
            SessionTime,
            CurrentSpot,
            TotalDamage,
            AverageDps,
            Kills,
            AverageKillTime,
            ExpGained,
            ExpPerHour,
            ZenPicked,
            ZenPerHour,
            JewelsPicked,
            HpPotionsUsed,
            MpPotionsUsed,
            IdleTime,
            StuckTime,
            LastDeathReason,
            Count,
        };

        static constexpr int TEXT_COUNT = static_cast<int>(TextId::Count);
        static constexpr int METRIC_COUNT = static_cast<int>(MetricId::Count);
        static constexpr int TEXT_LENGTH = 40;
        static constexpr int VALUE_LENGTH = 56;

        void LoadText();
        void LoadMetricText();
        void LoadStateText();
        void RefreshValues();
        void RefreshDamageValues(wchar_t* value, int valueLength);
        void RefreshKillEconomyValues(wchar_t* value, int valueLength);
        void RefreshPotionStateValues(wchar_t* value, int valueLength);
        void RenderBackground() const;
        void RenderHeader() const;
        void RenderRows() const;
        void RenderMetric(int metricIndex, int x, int y, int width) const;
        bool HandleCloseButton();
        bool HandleDragging();
        void ClampToScreen();

        void SetValue(MetricId metricId, const wchar_t* value);
        const wchar_t* GetText(TextId textId) const;
        TextId GetMetricLabel(MetricId metricId) const;
        void FormatCurrentSpot(wchar_t* output, int outputLength) const;
        void FormatDeathReason(GameLogic::Helper::SessionStats::DeathReason reason, wchar_t* output, int outputLength) const;

        SEASON3B::CNewUIManager* m_NewUIManager;
        POINT m_Pos;
        bool m_IsDragging;
        int m_DragOffsetX;
        int m_DragOffsetY;
        DWORD m_LastRefreshTick;
        wchar_t m_Text[TEXT_COUNT][TEXT_LENGTH];
        wchar_t m_Values[METRIC_COUNT][VALUE_LENGTH];
        GameLogic::Helper::SessionStats::Snapshot m_Snapshot;
    };
}
