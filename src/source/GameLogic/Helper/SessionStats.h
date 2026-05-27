#pragma once

namespace GameLogic::Helper::SessionStats
{
    inline constexpr int DPS_ROLLING_WINDOW_SECONDS = 5;
    inline constexpr int IDLE_THRESHOLD_MS = 5000;
    inline constexpr int STUCK_THRESHOLD_MS = 3000;
    inline constexpr int NO_TARGET_THRESHOLD_MS = 2000;
    inline constexpr int RECENT_TARGET_TTL_MS = 8000;
    inline constexpr int RECENT_TARGET_CAPACITY = 64;

    enum class DamageKind
    {
        Normal,
        Bonus,
        Elemental,
    };

    enum class PotionKind
    {
        HP,
        MP,
    };

    enum class Warning
    {
        None,
        LowDurability,
        NoArrowsBolts,
        NoMana,
        NoPotions,
        InventoryFull,
        Disconnected,
        Unknown,
    };

    enum class DeathReason
    {
        None,
        KilledByMonster,
        KilledByPK,
        Disconnected,
        Unknown,
    };

    struct Snapshot
    {
        bool hasSession = false;
        bool isActive = false;
        bool bonusDamageKnown = true;
        bool elementalDamageKnown = false;
        unsigned int activeMilliseconds = 0;
        unsigned int activeSeconds = 0;
        unsigned long long totalDamage = 0;
        unsigned long long normalDamage = 0;
        unsigned long long bonusDamage = 0;
        unsigned long long elementalDamage = 0;
        unsigned int averageDps = 0;
        unsigned int peakDps = 0;
        unsigned int normalDps = 0;
        unsigned int bonusDps = 0;
        unsigned int elementalDps = 0;
        unsigned int kills = 0;
        unsigned int killsPerMinute10 = 0;
        unsigned int averageKillTimeCentiseconds = 0;
        unsigned long long experienceGained = 0;
        unsigned long long experiencePerHour = 0;
        unsigned long long zenPicked = 0;
        unsigned long long zenPerHour = 0;
        unsigned int jewelsPicked = 0;
        unsigned int potionsUsed = 0;
        unsigned int hpPotionsUsed = 0;
        unsigned int mpPotionsUsed = 0;
        unsigned int idleSeconds = 0;
        unsigned int stuckSeconds = 0;
        unsigned int noTargetSeconds = 0;
        Warning lastWarning = Warning::None;
        DeathReason lastDeathReason = DeathReason::None;
    };

    void Start();
    void Stop();
    void Reset();
    bool IsActive();
    void Tick(int deltaMilliseconds, int positionX, int positionY, bool hasTarget);
    void RecordActivity();
    void RecordDamage(int damage, DamageKind kind);
    DamageKind ClassifyCombatDamage(int damageType, bool doubleDamage, bool comboDamage);
    void RecordKill();
    void RecordExperience(unsigned long long experience);
    void SampleExperience(long long currentBaseExperience, long long currentMasterExperience);
    void RecordZenPicked(int amount);
    void RecordJewelPicked();
    void RecordPotionUsed(PotionKind kind);
    void RecordMovementAttempt(int positionX, int positionY);
    void RecordWarning(Warning warning);
    void RecordDeath(DeathReason reason);
    void RecordDisconnected();
    void RegisterHeroTarget(int monsterKey);
    bool IsRecentHeroTarget(int monsterKey);
    void GetSnapshot(Snapshot& snapshot);
}
