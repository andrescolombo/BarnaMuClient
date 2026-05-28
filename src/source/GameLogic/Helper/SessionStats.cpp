#include "stdafx.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>

#include "GameLogic/Helper/SessionStats.h"

namespace GameLogic::Helper::SessionStats
{
    namespace
    {
        constexpr int ONE_SECOND_MS = 1000;
        constexpr int ONE_MINUTE_MS = 60000;
        constexpr int ONE_HOUR_MS = 3600000;
        constexpr int DAMAGE_TYPE_PERFECT = 1;
        constexpr int DAMAGE_TYPE_EXCELLENT = 2;
        constexpr int DAMAGE_TYPE_CRITICAL = 3;

        struct RecentTarget
        {
            int key = 0;
            DWORD expireTick = 0;
        };

        struct State
        {
            bool hasSession = false;
            bool isActive = false;
            RecentTarget recentTargets[RECENT_TARGET_CAPACITY] = {};
            unsigned int recentTargetWriteIndex = 0;
            unsigned int activeMilliseconds = 0;
            unsigned long long totalDamage = 0;
            unsigned long long normalDamage = 0;
            unsigned long long bonusDamage = 0;
            unsigned long long elementalDamage = 0;
            unsigned long long damageBuckets[DPS_ROLLING_WINDOW_SECONDS] = {};
            unsigned long long currentSecondDamage = 0;
            unsigned long long rollingDamage = 0;
            unsigned int currentSecondMilliseconds = 0;
            unsigned int currentDamageBucket = 0;
            unsigned int completedDamageBuckets = 0;
            unsigned int peakDps = 0;
            unsigned int kills = 0;
            unsigned long long experienceGained = 0;
            unsigned long long zenPicked = 0;
            unsigned int jewelsPicked = 0;
            unsigned int hpPotionsUsed = 0;
            unsigned int mpPotionsUsed = 0;
            unsigned int idleMilliseconds = 0;
            unsigned int stuckMilliseconds = 0;
            unsigned int noTargetMilliseconds = 0;
            unsigned int lastActivityMilliseconds = 0;
            unsigned int noTargetProbeMilliseconds = 0;
            unsigned int stationaryMoveMilliseconds = 0;
            int lastPositionX = 0;
            int lastPositionY = 0;
            bool hasLastPosition = false;
            bool movementAttemptThisTick = false;
            Warning lastWarning = Warning::None;
            DeathReason lastDeathReason = DeathReason::None;
            long long lastBaseExperienceSample = -1;
            long long lastMasterExperienceSample = -1;
        };

        State g_State;
        std::mutex g_StateLock;

        void ResetState()
        {
            State freshState;
            g_State = freshState;
        }

        unsigned int ClampToUInt(unsigned long long value)
        {
            constexpr auto maxValue = std::numeric_limits<unsigned int>::max();
            return static_cast<unsigned int>(std::min<unsigned long long>(value, maxValue));
        }

        unsigned int GetRateSeconds()
        {
            const unsigned int seconds = g_State.activeMilliseconds / ONE_SECOND_MS;
            return std::max(1u, seconds);
        }

        unsigned long long GetHourlyRate(unsigned long long value)
        {
            if (g_State.activeMilliseconds == 0)
            {
                return 0;
            }

            return value * ONE_HOUR_MS / g_State.activeMilliseconds;
        }

        void CommitDamageSecond()
        {
            g_State.rollingDamage -= g_State.damageBuckets[g_State.currentDamageBucket];
            g_State.damageBuckets[g_State.currentDamageBucket] = g_State.currentSecondDamage;
            g_State.rollingDamage += g_State.currentSecondDamage;
            g_State.currentDamageBucket = (g_State.currentDamageBucket + 1) % DPS_ROLLING_WINDOW_SECONDS;
            g_State.completedDamageBuckets = std::min(g_State.completedDamageBuckets + 1u, static_cast<unsigned int>(DPS_ROLLING_WINDOW_SECONDS));
            g_State.currentSecondDamage = 0;
        }

        void UpdatePeakDamage()
        {
            if (g_State.completedDamageBuckets == 0)
            {
                return;
            }

            const auto dps = g_State.rollingDamage / g_State.completedDamageBuckets;
            g_State.peakDps = std::max(g_State.peakDps, ClampToUInt(dps));
        }

        void UpdateRollingDamage(int deltaMilliseconds)
        {
            g_State.currentSecondMilliseconds += deltaMilliseconds;
            while (g_State.currentSecondMilliseconds >= ONE_SECOND_MS)
            {
                g_State.currentSecondMilliseconds -= ONE_SECOND_MS;
                CommitDamageSecond();
                UpdatePeakDamage();
            }
        }

        void UpdateIdleTime(int deltaMilliseconds)
        {
            if (g_State.activeMilliseconds - g_State.lastActivityMilliseconds >= IDLE_THRESHOLD_MS)
            {
                g_State.idleMilliseconds += deltaMilliseconds;
            }
        }

        void UpdateNoTargetTime(int deltaMilliseconds, bool hasTarget)
        {
            if (hasTarget)
            {
                g_State.noTargetProbeMilliseconds = 0;
                return;
            }

            g_State.noTargetProbeMilliseconds += deltaMilliseconds;
            if (g_State.noTargetProbeMilliseconds >= NO_TARGET_THRESHOLD_MS)
            {
                g_State.noTargetMilliseconds += deltaMilliseconds;
            }
        }

        void UpdateStuckTime(int deltaMilliseconds, int positionX, int positionY)
        {
            if (!g_State.movementAttemptThisTick || !g_State.hasLastPosition)
            {
                g_State.stationaryMoveMilliseconds = 0;
                return;
            }

            const bool hasMoved = positionX != g_State.lastPositionX || positionY != g_State.lastPositionY;
            g_State.stationaryMoveMilliseconds = hasMoved ? 0 : g_State.stationaryMoveMilliseconds + deltaMilliseconds;
            if (g_State.stationaryMoveMilliseconds >= STUCK_THRESHOLD_MS)
            {
                g_State.stuckMilliseconds += deltaMilliseconds;
            }
        }

        void SetLastPosition(int positionX, int positionY)
        {
            g_State.lastPositionX = positionX;
            g_State.lastPositionY = positionY;
            g_State.hasLastPosition = true;
            g_State.movementAttemptThisTick = false;
        }

        void FillRates(Snapshot& snapshot)
        {
            const unsigned int seconds = GetRateSeconds();
            snapshot.averageDps = ClampToUInt(g_State.totalDamage / seconds);
            snapshot.normalDps = ClampToUInt(g_State.normalDamage / seconds);
            snapshot.bonusDps = ClampToUInt(g_State.bonusDamage / seconds);
            snapshot.elementalDps = ClampToUInt(g_State.elementalDamage / seconds);
            snapshot.peakDps = g_State.peakDps;
            snapshot.experiencePerHour = GetHourlyRate(g_State.experienceGained);
            snapshot.zenPerHour = GetHourlyRate(g_State.zenPicked);
        }

        void FillKillRates(Snapshot& snapshot)
        {
            if (g_State.activeMilliseconds == 0 || g_State.kills == 0)
            {
                return;
            }

            snapshot.killsPerMinute10 = ClampToUInt(
                static_cast<unsigned long long>(g_State.kills) * ONE_MINUTE_MS * 10 / g_State.activeMilliseconds);
            snapshot.averageKillTimeCentiseconds = g_State.activeMilliseconds / 10 / g_State.kills;
        }

        void FillCounters(Snapshot& snapshot)
        {
            snapshot.totalDamage = g_State.totalDamage;
            snapshot.normalDamage = g_State.normalDamage;
            snapshot.bonusDamage = g_State.bonusDamage;
            snapshot.elementalDamage = g_State.elementalDamage;
            snapshot.kills = g_State.kills;
            snapshot.experienceGained = g_State.experienceGained;
            snapshot.zenPicked = g_State.zenPicked;
            snapshot.jewelsPicked = g_State.jewelsPicked;
            snapshot.hpPotionsUsed = g_State.hpPotionsUsed;
            snapshot.mpPotionsUsed = g_State.mpPotionsUsed;
            snapshot.potionsUsed = g_State.hpPotionsUsed + g_State.mpPotionsUsed;
        }

        void FillDurations(Snapshot& snapshot)
        {
            snapshot.activeMilliseconds = g_State.activeMilliseconds;
            snapshot.activeSeconds = g_State.activeMilliseconds / ONE_SECOND_MS;
            snapshot.idleSeconds = g_State.idleMilliseconds / ONE_SECOND_MS;
            snapshot.stuckSeconds = g_State.stuckMilliseconds / ONE_SECOND_MS;
            snapshot.noTargetSeconds = g_State.noTargetMilliseconds / ONE_SECOND_MS;
        }
    }

    void Start()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        ResetState();
        g_State.hasSession = true;
        g_State.isActive = true;
    }

    void Stop()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        g_State.isActive = false;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        ResetState();
    }

    bool IsActive()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        return g_State.isActive;
    }

    void Tick(int deltaMilliseconds, int positionX, int positionY, bool hasTarget)
    {
        if (deltaMilliseconds <= 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.activeMilliseconds += deltaMilliseconds;
        UpdateRollingDamage(deltaMilliseconds);
        UpdateIdleTime(deltaMilliseconds);
        UpdateNoTargetTime(deltaMilliseconds, hasTarget);
        UpdateStuckTime(deltaMilliseconds, positionX, positionY);
        SetLastPosition(positionX, positionY);
    }

    void RecordActivity()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.lastActivityMilliseconds = g_State.activeMilliseconds;
    }

    void RecordDamage(int damage, DamageKind kind)
    {
        if (damage <= 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        const auto value = static_cast<unsigned long long>(damage);
        g_State.totalDamage += value;
        g_State.currentSecondDamage += value;
        g_State.lastActivityMilliseconds = g_State.activeMilliseconds;

        if (kind == DamageKind::Bonus)
        {
            g_State.bonusDamage += value;
            return;
        }
        if (kind == DamageKind::Elemental)
        {
            g_State.elementalDamage += value;
            return;
        }
        g_State.normalDamage += value;
    }

    DamageKind ClassifyCombatDamage(int damageType, bool doubleDamage, bool comboDamage)
    {
        if (doubleDamage || comboDamage)
        {
            return DamageKind::Bonus;
        }

        if (damageType == DAMAGE_TYPE_PERFECT
            || damageType == DAMAGE_TYPE_EXCELLENT
            || damageType == DAMAGE_TYPE_CRITICAL)
        {
            return DamageKind::Bonus;
        }

        return DamageKind::Normal;
    }

    void RecordKill()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.kills++;
        g_State.lastActivityMilliseconds = g_State.activeMilliseconds;
    }

    void RecordExperience(unsigned long long experience)
    {
        if (experience == 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.experienceGained += experience;
    }

    void SampleExperience(long long currentBaseExperience, long long currentMasterExperience)
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        const long long lastBase = g_State.lastBaseExperienceSample;
        const long long lastMaster = g_State.lastMasterExperienceSample;
        g_State.lastBaseExperienceSample = currentBaseExperience;
        g_State.lastMasterExperienceSample = currentMasterExperience;

        if (lastBase >= 0 && currentBaseExperience > lastBase)
        {
            g_State.experienceGained += static_cast<unsigned long long>(currentBaseExperience - lastBase);
        }
        if (lastMaster >= 0 && currentMasterExperience > lastMaster)
        {
            g_State.experienceGained += static_cast<unsigned long long>(currentMasterExperience - lastMaster);
        }
    }

    void RecordZenPicked(int amount)
    {
        if (amount <= 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.zenPicked += static_cast<unsigned int>(amount);
    }

    void RecordJewelPicked()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.jewelsPicked++;
    }

    void RecordPotionUsed(PotionKind kind)
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        if (kind == PotionKind::HP)
        {
            g_State.hpPotionsUsed++;
        }
        else
        {
            g_State.mpPotionsUsed++;
        }
    }

    void RecordMovementAttempt(int positionX, int positionY)
    {
        (void)positionX;
        (void)positionY;

        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.isActive)
        {
            return;
        }

        g_State.movementAttemptThisTick = true;
    }

    void RecordWarning(Warning warning)
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.hasSession)
        {
            return;
        }

        g_State.lastWarning = warning;
    }

    void RecordDeath(DeathReason reason)
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.hasSession)
        {
            return;
        }

        g_State.lastDeathReason = reason;
    }

    void RegisterHeroTarget(int monsterKey)
    {
        if (monsterKey == 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_StateLock);
        const DWORD expireTick = GetTickCount() + RECENT_TARGET_TTL_MS;
        for (auto& target : g_State.recentTargets)
        {
            if (target.key == monsterKey)
            {
                target.expireTick = expireTick;
                return;
            }
        }

        g_State.recentTargets[g_State.recentTargetWriteIndex] = { monsterKey, expireTick };
        g_State.recentTargetWriteIndex = (g_State.recentTargetWriteIndex + 1) % RECENT_TARGET_CAPACITY;
    }

    bool IsRecentHeroTarget(int monsterKey)
    {
        if (monsterKey == 0)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_StateLock);
        const DWORD now = GetTickCount();
        for (const auto& target : g_State.recentTargets)
        {
            if (target.key == monsterKey && target.expireTick > now)
            {
                return true;
            }
        }
        return false;
    }

    void RecordDisconnected()
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        if (!g_State.hasSession)
        {
            return;
        }

        g_State.lastWarning = Warning::Disconnected;
        g_State.lastDeathReason = DeathReason::Disconnected;
        g_State.isActive = false;
    }

    void GetSnapshot(Snapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(g_StateLock);
        snapshot = Snapshot();
        snapshot.hasSession = g_State.hasSession;
        snapshot.isActive = g_State.isActive;
        snapshot.lastWarning = g_State.lastWarning;
        snapshot.lastDeathReason = g_State.lastDeathReason;
        FillCounters(snapshot);
        FillDurations(snapshot);
        FillRates(snapshot);
        FillKillRates(snapshot);
    }
}
