#include "stdafx.h"

#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>

#include "Engine/AI/ZzzAI.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Engine/Object/ZzzInterface.h"
#include "UI/NewUI/NewUISystem.h"
#include "Core/Utilities/Log/muConsoleDebug.h"
#include "Character/CharacterManager.h"
#include "GameLogic/Helper/SessionStats.h"
#include "GameLogic/Skills/SkillManager.h"
#include "GameLogic/Social/PartyManager.h"
#include "World/MapInfra/MapManager.h"
#include "Network/Server/WSclient.h"

#include "MuHelper.h"

constexpr int MAX_ACTIONABLE_DISTANCE = 10;
constexpr int DEFAULT_DURABILITY_THRESHOLD = 50;
constexpr int MUHELPER_TIMER_INTERVAL_MS = 50;
constexpr DWORD OWN_DROP_TTL_MS = 5000;
constexpr int OWN_DROP_TILE_TOLERANCE = 1;

SpinLock _targetsLock;
SpinLock _itemsLock;

// Movement/target globals are defined in ZzzInterface.cpp.
extern MovementSkill g_MovementSkill;
extern int SelectedCharacter;
extern int TargetX;
extern int TargetY;

namespace MUHelper
{
	MovementSkill& g_MovementSkill = ::g_MovementSkill;
	int& SelectedCharacter = ::SelectedCharacter;
	int& TargetX = ::TargetX;
	int& TargetY = ::TargetY;

    CMuHelper g_MuHelper;

    void CALLBACK CMuHelper::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
    {
        g_MuHelper.WorkLoop(hwnd, uMsg, idEvent, dwTime);
    }

    void CMuHelper::Save(const ConfigData& config)
    {
        m_config = config;

        PRECEIVE_MUHELPER_DATA netData;
        ConfigDataSerDe::Serialize(m_config, netData);

        SocketClient->ToGameServer()->SendMuHelperSaveDataRequest(reinterpret_cast<BYTE*>(&netData), sizeof(netData));
    }

    void CMuHelper::Load(const ConfigData& config)
    {
        m_config = config;
    }

    ConfigData CMuHelper::GetConfig() const {
        return m_config;
    }

    void CMuHelper::Toggle()
    {
        if (m_bActive)
        {
            TriggerStop();

            // Stop the client-driven bot immediately instead of waiting for the
            // server's status reply. After an auto-reconnect the server's new
            // session doesn't have the helper marked active, so it never replies
            // and the bot would otherwise keep running with no way to stop it.
            Stop();
        }
        else
        {
            TriggerStart();
        }
    }

    void CMuHelper::TriggerStart()
    {
        if (!Hero->SafeZone)
            SocketClient->ToGameServer()->SendMuHelperStatusChangeRequest(0);
    }

    void CMuHelper::TriggerStop()
    {
        SocketClient->ToGameServer()->SendMuHelperStatusChangeRequest(1);
    }

    void CMuHelper::Start()
    {
        if (m_bActive)
        {
            return;
        }

        m_iTotalCost = 0;
        m_iComboState = 0;
        m_iCurrentBuffIndex = 0;
        m_iCurrentBuffPartyIndex = 0;
        m_iCurrentHealPartyIndex = 0;
        m_iCurrentTarget = -1;
        m_iCurrentSkill = (ActionSkillType)m_config.aiSkill[0];
        m_iCurrentItem = MAX_ITEMS;
        m_iLastObtainItem = MAX_ITEMS;
        m_iObtainStuckTicks = 0;
        m_setSkippedItems.clear();
        m_bZenCleanupMode = false;
        m_posOriginal = { Hero->PositionX, Hero->PositionY };

        m_iHuntingDistance = ComputeDistanceByRange(m_config.iHuntingRange);
        m_iObtainingDistance = ComputeDistanceByRange(m_config.iObtainingRange);

        m_iSecondsElapsed = 0;
        m_iSecondsAway = 0;

        m_bTimerActivatedBuffOngoing = false;
        m_bPetActivated = false;

        m_iLoopCounter = 0;

        m_bActive = true;
        GameLogic::Helper::SessionStats::Start();
        g_pNewUISystem->Show(SEASON3B::INTERFACE_HELPER_SESSION_STATUS);
        g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Started");
    }

    void CMuHelper::Stop()
    {
        m_bActive = false;
        GameLogic::Helper::SessionStats::Stop();
        g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Stopped");
    }

    void CMuHelper::WorkLoop(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
    {
        if (!m_bActive)
        {
            return;
        }

        if (Hero->SafeZone)
        {
            g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Entered safezone. Stopping.");
            TriggerStop();
            return;
        }

        GameLogic::Helper::SessionStats::Tick(
            MUHELPER_TIMER_INTERVAL_MS,
            Hero->PositionX,
            Hero->PositionY,
            HasAnyTarget());

        GameLogic::Helper::SessionStats::SampleExperience(
            static_cast<long long>(CharacterAttribute->Experience),
            Master_Level_Data.lMasterLevel_Experince);

        Work();

        if (m_iLoopCounter++ == 4)
        {
            m_iSecondsElapsed++;

            if (ComputeDistanceBetween({ Hero->PositionX, Hero->PositionY }, m_posOriginal) > 1)
            {
                m_iSecondsAway++;
            }
            else
            {
                m_iSecondsAway = 0;
            }

            m_iLoopCounter = 0;
        }
    }

    void CMuHelper::Work()
    {
        try
        {
            if (!ActivatePet())
            {
                return;
            }

            if (!Buff())
            {
                return;
            }

            if (!RecoverHealth())
            {
                return;
            }

            if (!ObtainItem())
            {
                return;
            }

            if (!Regroup())
            {
                return;
            }

            Attack();

            RepairEquipments();
        }
        catch (...)
        {
            g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Exception occurred. Ignoring...");
        }
    }

    void CMuHelper::AddTarget(int iTargetId, bool bIsAttacking)
    {
        if (!m_bActive)
        {
            return;
        }

        CHARACTER* pTarget = FindCharacterByKey(iTargetId);
        if (!pTarget || pTarget == Hero)
        {
            return;
        }

        int iDistance = ComputeDistanceFromTarget(pTarget);

        if ((iDistance <= m_iHuntingDistance)
            || (bIsAttacking && m_config.bLongRangeCounterAttack))
        {
            _targetsLock.lock();

            m_setTargets.insert(iTargetId);

            if (bIsAttacking)
            {
                m_setTargetsAttacking.insert(iTargetId);
            }

            _targetsLock.unlock();
        }

        if (m_config.bUseSelfDefense && IsMonster(pTarget))
        {
            m_iCurrentTarget = iTargetId;
        }
    }

    void CMuHelper::DeleteTarget(int iTargetId)
    {
        _targetsLock.lock();

        m_setTargets.erase(iTargetId);
        m_setTargetsAttacking.erase(iTargetId);

        _targetsLock.unlock();

        if (iTargetId == m_iCurrentTarget)
        {
            m_iCurrentTarget = -1;
        }

        // Killing/losing a mob may have freed a previously-blocked drop tile.
        // Re-arm the skip-set so ObtainItem gets another shot at it.
        m_setSkippedItems.clear();
    }

    void CMuHelper::DeleteAllTargets()
    {
        _targetsLock.lock();

        m_setTargets.clear();
        m_setTargetsAttacking.clear();

        _targetsLock.unlock();
    }

    int CMuHelper::ComputeDistanceByRange(int iRange)
    {
        return ComputeDistanceBetween({ 0, 0 }, { iRange, iRange });
    }

    int CMuHelper::ComputeDistanceFromTarget(CHARACTER* pTarget)
    {
        const POINT posHero = { Hero->PositionX, Hero->PositionY };

        const POINT posCurrent = { pTarget->PositionX, pTarget->PositionY };
        const POINT posNext    = { pTarget->TargetX,   pTarget->TargetY };

        return std::min(
            ComputeDistanceBetween(posHero, posCurrent),
            ComputeDistanceBetween(posHero, posNext)
        );
    }

    int CMuHelper::ComputeDistanceBetween(POINT posA, POINT posB)
    {
        int iDx = posA.x - posB.x;
        int iDy = posA.y - posB.y;

        return static_cast<int>(std::ceil(std::sqrt(iDx * iDx + iDy * iDy)));
    }

    int CMuHelper::GetBasicAttackIntervalMs() const
    {
        // Mimic Webzen swing cadence: ~1.0 swings/s at AttackSpeed=0, scaling
        // linearly with the stat, clamped at 10 swings/s.
        const int as = CharacterAttribute ? CharacterAttribute->AttackSpeed : 0;
        int ms = 1000 - (as * 2);
        if (ms < 100) ms = 100;
        return ms;
    }

    bool CMuHelper::HasAnyTarget() const
    {
        return m_iCurrentTarget != -1 || !m_setTargets.empty();
    }

    int CMuHelper::GetNearestTarget()
    {
        int iClosestMonsterId = -1;
        int iMinDistance = m_iHuntingDistance;
        std::set<int> setTargets;
        {
            _targetsLock.lock();
            setTargets = m_setTargets;
            _targetsLock.unlock();
        }

        for (const int& iMonsterId : setTargets)
        {
            int iIndex = FindCharacterIndex(iMonsterId);
            CHARACTER* pTarget = &CharactersClient[iIndex];

            if (!IsMonster(pTarget))
            {
                continue;
            }

            int iDistance = ComputeDistanceFromTarget(pTarget);
            if (iDistance <= iMinDistance)
            {
                iMinDistance = iDistance;
                iClosestMonsterId = iMonsterId;
            }
        }

        return iClosestMonsterId;
    }

    int CMuHelper::GetFarthestAttackingTarget()
    {
        int iFarthestMonsterId = -1;
        int iMaxDistance = -1;

        std::set<int> setTargets;
        {
            _targetsLock.lock();
            setTargets = m_setTargetsAttacking;
            _targetsLock.unlock();
        }

        for (const int& iMonsterId : setTargets)
        {
            int iIndex = FindCharacterIndex(iMonsterId);
            CHARACTER* pTarget = &CharactersClient[iIndex];

            if (!IsMonster(pTarget))
            {
                continue;
            }

            int iDistance = ComputeDistanceFromTarget(pTarget);
            if (iDistance > iMaxDistance)
            {
                iMaxDistance = iDistance;
                iFarthestMonsterId = iMonsterId;
            }
        }

        return iFarthestMonsterId;
    }

    void CMuHelper::CleanupTargets()
    {
        std::set<int> setTargets;
        {
            _targetsLock.lock();
            setTargets = m_setTargets;
            _targetsLock.unlock();
        }

        for (const int& iMonsterId : setTargets)
        {
            int iIndex = FindCharacterIndex(iMonsterId);
            if (iIndex == MAX_CHARACTERS_CLIENT)
            {
                DeleteTarget(iMonsterId);
                continue;
            }

            CHARACTER* pTarget = &CharactersClient[iIndex];
            if (pTarget->Dead > 0 || !pTarget->Object.Live)
            {
                DeleteTarget(iMonsterId);
            }
        }
    }

    int CMuHelper::ActivatePet()
    {
        if (!m_config.bUseDarkRaven)
        {
            return 1;
        }

        if (m_bPetActivated)
        {
            return 1;
        }

        if (m_config.iDarkRavenMode == PET_ATTACK_CEASE)
        {
            SocketClient->ToGameServer()->SendPetCommandRequest(PetType::DarkRaven, PetCommandMode::Normal, 0xFFFF);
        }
        else if (m_config.iDarkRavenMode == PET_ATTACK_AUTO)
        {
            SocketClient->ToGameServer()->SendPetCommandRequest(PetType::DarkRaven, PetCommandMode::AttackRandom, 0xFFFF);
        }
        else if (m_config.iDarkRavenMode == PET_ATTACK_TOGETHER)
        {
            SocketClient->ToGameServer()->SendPetCommandRequest(PetType::DarkRaven, PetCommandMode::AttackWithOwner, 0xFFFF);
        }

        m_bPetActivated = true;
        return 1;
    }

    int CMuHelper::Buff()
    {
        if (!HasAssignedBuffSkill())
        {
            return 1;
        }

        if (m_config.bSupportParty && g_pPartyManager->IsPartyActive())
        {
            PARTY_t* pMember = &Party[m_iCurrentBuffPartyIndex];
            CHARACTER* pChar = g_pPartyManager->GetPartyMemberChar(pMember);

            if (pChar != NULL
                && pMember->Map == gMapManager.WorldActive
                && ComputeDistanceFromTarget(pChar) <= MAX_ACTIONABLE_DISTANCE)
            {
                if (!m_config.bBuffDurationParty
                    && m_config.iBuffCastInterval != 0
                    && m_iSecondsElapsed % m_config.iBuffCastInterval == 0)
                {
                    m_bTimerActivatedBuffOngoing = true;
                }

                if (!BuffTarget(pChar, (ActionSkillType)m_config.aiBuff[m_iCurrentBuffIndex]))
                {
                    return 0;
                }
            }

            m_iCurrentBuffPartyIndex = (m_iCurrentBuffPartyIndex + 1) % (sizeof(Party) / sizeof(Party[0]));
        }
        else
        {
            if (!m_config.bBuffDuration
                && m_config.iBuffCastInterval != 0
                && m_iSecondsElapsed % m_config.iBuffCastInterval == 0)
            {
                m_bTimerActivatedBuffOngoing = true;
            }

            if (!BuffTarget(Hero, (ActionSkillType)m_config.aiBuff[m_iCurrentBuffIndex]))
            {
                return 0;
            }
        }

        if (m_iCurrentBuffPartyIndex == 0)
        {
            m_iCurrentBuffIndex = (m_iCurrentBuffIndex + 1) % m_config.aiBuff.size();

            // Reaching this branch means everyone's been buffed, 
            // so we're resetting the timer activated buff flag
            if (m_iCurrentBuffIndex == 0)
            {
                m_bTimerActivatedBuffOngoing = false;
            }
        }

        return 1;
    }

    int CMuHelper::BuffTarget(CHARACTER* pTargetChar, ActionSkillType iBuffSkill)
    {
        // TODO: List other buffs here
        OBJECT* obj = &pTargetChar->Object;

        auto CastIfMissing = [&](bool bBuffActive, bool bTimerRespected, bool bNeedsTarget) -> int
        {
            if (!bBuffActive || (bTimerRespected && m_bTimerActivatedBuffOngoing))
                return SimulateSkill(iBuffSkill, bNeedsTarget, pTargetChar->Key);
            return 1;
        };

        switch (iBuffSkill)
        {
        case AT_SKILL_ATTACK:
        case AT_SKILL_ATTACK_STR:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_Attack), true, true);

        case AT_SKILL_DEFENSE:
        case AT_SKILL_DEFENSE_STR:
        case AT_SKILL_DEFENSE_MASTERY:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_Defense), true, true);

        case AT_SKILL_INFINITY_ARROW:
        case AT_SKILL_INFINITY_ARROW_STR:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_InfinityArrow), false, false);

        case AT_SKILL_SOUL_BARRIER:
        case AT_SKILL_SOUL_BARRIER_STR:
        case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_WizDefense), true, true);

        case AT_SKILL_SWELL_LIFE:
        case AT_SKILL_SWELL_LIFE_STR:
        case AT_SKILL_SWELL_LIFE_PROFICIENCY:
            if (m_iComboState == 2)
            {
                return 1;
            }
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_Life), true, false);

        case AT_SKILL_EXPANSION_OF_WIZARDRY:
        case AT_SKILL_EXPANSION_OF_WIZARDRY_STR:
        case AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_SwellOfMagicPower), false, false);

        case AT_SKILL_ADD_CRITICAL:
        case AT_SKILL_ADD_CRITICAL_STR1:
        case AT_SKILL_ADD_CRITICAL_STR2:
        case AT_SKILL_ADD_CRITICAL_STR3:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_AddCriticalDamage), false, false);

        case AT_SKILL_ALICE_BERSERKER:
        case AT_SKILL_ALICE_BERSERKER_STR:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_Berserker), false, false);

        case AT_SKILL_ALICE_THORNS:
            return CastIfMissing(g_isCharacterBuff(obj, eBuff_Thorns), false, false);

        default:
            return 1;
        }
    }

    int CMuHelper::ConsumePotion()
    {
        int64_t iLife = CharacterAttribute->Life;
        int64_t iLifeMax = CharacterAttribute->LifeMax;

        if (m_config.bUseHealPotion && iLifeMax > 0 && iLife > 0)
        {
            int64_t iRemaining = (iLife * 100 + iLifeMax - 1) / iLifeMax;
            if (iRemaining <= m_config.iPotionThreshold)
            {
                int iPotionIndex = g_pMyInventory->FindHealingItemIndex();
                if (iPotionIndex != -1)
                {
                    SendRequestUse(iPotionIndex, 0);
                }
            }
        }

        return 1;
    }

    int CMuHelper::RecoverHealth()
    {
        if (!Heal())
        {
            return 0;
        }
        
        if (!DrainLife())
        {
            return 0;
        }

        if (!ConsumePotion())
        {
            return 0;
        }

        return 1;
    }

    int CMuHelper::Heal()
    {
        if (!m_config.bAutoHeal)
        {
            return 1;
        }

        auto iHealingSkill = GetHealingSkill();
        if (iHealingSkill == AT_SKILL_UNDEFINED)
        {
            return 1;
        }

        if (m_config.bAutoHealParty && g_pPartyManager->IsPartyActive())
        {
            PARTY_t* pMember = &Party[m_iCurrentHealPartyIndex];
            CHARACTER* pChar = g_pPartyManager->GetPartyMemberChar(pMember);
            int iHealResult = 1;

            if (pChar != NULL)
            {
                if (pChar == Hero)
                {
                    iHealResult = HealSelf(iHealingSkill);
                }
                else if (pMember->Map == gMapManager.WorldActive
                    && pMember->stepHP * 10 <= m_config.iHealPartyThreshold
                    && ComputeDistanceFromTarget(pChar) <= MAX_ACTIONABLE_DISTANCE)
                {
                    iHealResult = SimulateSkill(iHealingSkill, true, pChar->Key);
                }
            }

            m_iCurrentHealPartyIndex = (m_iCurrentHealPartyIndex + 1) % (sizeof(Party) / sizeof(Party[0]));

            return iHealResult;
        }
        else
        {
            return HealSelf(iHealingSkill);
        }

        return 1;
    }

    int CMuHelper::HealSelf(ActionSkillType iHealingSkill)
    {
        int64_t iLife = CharacterAttribute->Life;
        int64_t iLifeMax = CharacterAttribute->LifeMax;
        int64_t iRemaining = (iLife * 100 + iLifeMax - 1) / iLifeMax;

        if (iRemaining <= m_config.iHealThreshold)
        {
            return SimulateSkill(iHealingSkill, true, HeroKey);
        }

        return 1;
    }

    int CMuHelper::DrainLife()
    {
        if (!m_config.bUseDrainLife)
        {
            return 1;
        }

        auto iDrainLife = GetDrainLifeSkill();
        if (iDrainLife == AT_SKILL_UNDEFINED)
        {
            return 1;
        }

        int64_t iLife = CharacterAttribute->Life;
        int64_t iLifeMax = CharacterAttribute->LifeMax;
        int64_t iRemaining = (iLife * 100 + iLifeMax - 1) / iLifeMax;

        if (iRemaining <= m_config.iHealThreshold)
        {
            m_iCurrentTarget = GetNearestTarget();
            if (m_iCurrentTarget != -1)
            {
                return SimulateSkill(iDrainLife, true, m_iCurrentTarget);
            }
        }

        return 1;
    }

    int CMuHelper::RepairEquipments()
    {
        if (m_config.bRepairItem)
        {
            for (int i = 0; i < MAX_EQUIPMENT; i++)
            {
                ITEM* pItem = &CharacterMachine->Equipment[i];
                if (!pItem || pItem->Type == -1)
                {
                    continue;
                }

                ITEM_ATTRIBUTE* pAttr = &ItemAttribute[pItem->Type];
                if (!pAttr)
                {
                    continue;
                }

                int iLevel = pItem->Level;
                int iDurability = pItem->Durability;
                int iMaxDurability = CalcMaxDurability(pItem, pAttr, iLevel);

                int64_t iHealth = (iDurability * 100 + iMaxDurability - 1) / iMaxDurability;

                if (iHealth <= DEFAULT_DURABILITY_THRESHOLD)
                {
                    int64_t iGoldCost = CalcSelfRepairCost(ItemValue(pItem, 2), iDurability, iMaxDurability, pItem->Type);
                    if (iGoldCost <= CharacterMachine->Gold)
                    {
                        SocketClient->ToGameServer()->SendRepairItemRequest(i, 1);
                    }
                }
            }
        }

        return 1;
    }

    int CMuHelper::Attack()
    {
        if (m_iCurrentTarget == -1)
        {
            if (!m_setTargets.empty())
            {
                CleanupTargets();

                if (m_config.bLongRangeCounterAttack)
                {
                    m_iCurrentTarget = GetFarthestAttackingTarget();
                }
                
                if (m_iCurrentTarget == -1)
                {
                    m_iCurrentTarget = GetNearestTarget();
                }
            }
            else
            {
                m_iComboState = 0;
                return 0;
            }
        }

        if (m_config.bUseCombo)
        {
            return SimulateComboAttack();
        }

        m_iCurrentSkill = SelectAttackSkill();
        if (m_iCurrentSkill > AT_SKILL_UNDEFINED)
        {
            const float fSkillDistance = gSkillManager.GetSkillDistance(m_iCurrentSkill, Hero);
            if (CanExecuteSkill(Hero, m_iCurrentSkill, fSkillDistance))
            {
                return SimulateAttack(m_iCurrentSkill);
            }
        }

        if (m_config.bFallbackBasicAttack)
        {
            if (!Hero->Movement)
            {
                return SimulateBasicAttack(m_iCurrentTarget);
            }
        }

        return 1;
    }

    ActionSkillType CMuHelper::SelectAttackSkill()
    {
        const size_t safeSize = std::min({m_config.aiSkill.size(), m_config.aiSkillCondition.size(), m_config.aiSkillInterval.size()});
        for (int i = 1; i < (int)safeSize; i++)
        {
            const int iSkillId = m_config.aiSkill[i];
            if (iSkillId <= 0 || iSkillId >= MAX_SKILLS)
            {
                continue;
            }

            if ((m_config.aiSkillCondition[i] & ON_TIMER)
                && m_config.aiSkillInterval[i] != 0
                && m_iSecondsElapsed > 0
                && m_iSecondsElapsed % m_config.aiSkillInterval[i] == 0)
            {
                return (ActionSkillType)iSkillId;
            }

            if (m_config.aiSkillCondition[i] & ON_CONDITION)
            {
                int iCount = 0;
                if (m_config.aiSkillCondition[i] & ON_MOBS_NEARBY)
                {
                    iCount = (int)m_setTargets.size();
                }
                else if (m_config.aiSkillCondition[i] & ON_MOBS_ATTACKING)
                {
                    iCount = (int)m_setTargetsAttacking.size();
                }
                else
                {
                    continue;
                }

                if (((m_config.aiSkillCondition[i] & ON_MORE_THAN_TWO_MOBS)   && iCount >= 2)
                    || ((m_config.aiSkillCondition[i] & ON_MORE_THAN_THREE_MOBS) && iCount >= 3)
                    || ((m_config.aiSkillCondition[i] & ON_MORE_THAN_FOUR_MOBS)  && iCount >= 4)
                    || ((m_config.aiSkillCondition[i] & ON_MORE_THAN_FIVE_MOBS)  && iCount >= 5))
                {
                    return (ActionSkillType)iSkillId;
                }
            }
        }

        if (m_config.aiSkill[0] > 0)
        {
            return (ActionSkillType)m_config.aiSkill[0];
        }

        return AT_SKILL_UNDEFINED;
    }

    int CMuHelper::SimulateComboAttack()
    {
        for (int i = 0; i < m_config.aiSkill.size(); i++)
        {
            if (m_config.aiSkill[i] == 0)
            {
                return 0;
            }
        }

        if (SimulateAttack((ActionSkillType)m_config.aiSkill[m_iComboState]))
        {
            m_iComboState = (m_iComboState + 1) % 3;
        }

        return 1;
    }

    int CMuHelper::SimulateAttack(ActionSkillType iSkill)
    {
        return SimulateSkill(iSkill, true, m_iCurrentTarget);
    }

    int CMuHelper::SimulateSkill(ActionSkillType iSkill, bool bTargetRequired, int iTarget)
    {
        g_MovementSkill.m_iSkill = iSkill;
        g_MovementSkill.m_bMagic = true;

        const float fSkillDistance = gSkillManager.GetSkillDistance(iSkill, Hero);
        const bool bSelfPositionSkill = IsSelfPositionSkill(iSkill);

        if (bTargetRequired)
        {
            if (bSelfPositionSkill)
            {
                TargetX = Hero->PositionX;
                TargetY = Hero->PositionY;

                g_MovementSkill.m_iTarget = -1;

                // Check if current target is still valid (exists and alive)
                if (iTarget != -1)
                {
                    const int iCharIndex = FindCharacterIndex(iTarget);
                    if (iCharIndex != MAX_CHARACTERS_CLIENT)
                    {
                        CHARACTER* pCurrentTarget = &CharactersClient[iCharIndex];
                        if (pCurrentTarget->Dead > 0 || !IsMonster(pCurrentTarget))
                        {
                            DeleteTarget(iTarget);
                            return 0;
                        }
                    }
                    else
                    {
                        DeleteTarget(iTarget);
                        return 0;
                    }
                }
            }
            else
            {
                if (iTarget == -1)
                {
                    return 0;
                }

                const int iCharIndex = FindCharacterIndex(iTarget);
                if (iCharIndex == MAX_CHARACTERS_CLIENT)
                {
                    DeleteTarget(iTarget);
                    return 0;
                }

                SelectedCharacter = iCharIndex;

                CHARACTER* pTarget = &CharactersClient[iCharIndex];
                if (pTarget->Dead > 0)
                {
                    DeleteTarget(iTarget);
                    return 0;
                }

                g_MovementSkill.m_iTarget = iCharIndex;

                TargetX = (int)(pTarget->Object.Position[0] / TERRAIN_SCALE);
                TargetY = (int)(pTarget->Object.Position[1] / TERRAIN_SCALE);

                PATH_t tempPath;
                bool bHasPath = PathFinding2(Hero->PositionX, Hero->PositionY, TargetX, TargetY, &tempPath, m_iHuntingDistance + fSkillDistance);
                
                // Target not reachable, ignore it
                if (!bHasPath)
                {
                    DeleteTarget(iTarget);
                    return 0;
                }

                const bool bTargetNear = CheckTile(Hero, &Hero->Object, fSkillDistance);
                if (bTargetNear && !CheckWall(Hero->PositionX, Hero->PositionY, TargetX, TargetY))
                {
                    DeleteTarget(iTarget);
                    return 0;
                }

                // Target is not yet in range, move closer.
                if (!bTargetNear)
                {
                    Hero->Path.Lock.lock();

                    // Limit movement to 2 steps at a time
                    int pathNum = std::min<int>(tempPath.PathNum, 2);
                    for (int i = 0; i < pathNum; i++)
                    {
                        Hero->Path.PathX[i] = tempPath.PathX[i];
                        Hero->Path.PathY[i] = tempPath.PathY[i];
                    }
                    Hero->Path.PathNum = pathNum;
                    Hero->Path.CurrentPath = 0;
                    Hero->Path.CurrentPathFloat = 0;

                    Hero->Path.Lock.unlock();

                    SendMove(Hero, &Hero->Object);
                    return 0;
                }
            }
        }
        else
        {
            TargetX = Hero->PositionX;
            TargetY = Hero->PositionY;
        }

        int iSkillResult = ExecuteSkill(Hero, iSkill, fSkillDistance);
        if (iSkillResult == -1 && iTarget != -1)
        {
            DeleteTarget(iTarget);
        }
        if (iSkillResult == 1)
        {
            GameLogic::Helper::SessionStats::RecordActivity();
        }

        return (int)(iSkillResult == 1);
    }

    int CMuHelper::SimulateBasicAttack(int iTarget)
    {
        if (iTarget == -1)
        {
            return 0;
        }

        const int iCharIndex = FindCharacterIndex(iTarget);
        if (iCharIndex == MAX_CHARACTERS_CLIENT)
        {
            DeleteTarget(iTarget);
            return 0;
        }

        CHARACTER* pTarget = &CharactersClient[iCharIndex];
        if (pTarget->Dead > 0 || !IsMonster(pTarget))
        {
            DeleteTarget(iTarget);
            return 0;
        }

        // Basic-attack reach (matches Action() / MOVEMENT_ATTACK ranges in
        // ZzzInterface.cpp:3563-3579): default 1.8, spear 2.2, bow 6.0.
        constexpr float BASIC_RANGE_DEFAULT = 1.8f;
        constexpr float BASIC_RANGE_SPEAR = 2.2f;
        constexpr float BASIC_RANGE_BOW = 6.0f;

        float fRange = BASIC_RANGE_DEFAULT;
        const int iWeaponRight = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
        if (iWeaponRight >= ITEM_SPEAR && iWeaponRight < ITEM_SPEAR + MAX_ITEM_INDEX)
        {
            fRange = BASIC_RANGE_SPEAR;
        }
        if (gCharacterManager.GetEquipedBowType() != BOWTYPE_NONE)
        {
            fRange = BASIC_RANGE_BOW;
        }

        SelectedCharacter = iCharIndex;
        TargetX = (int)(pTarget->Object.Position[0] / TERRAIN_SCALE);
        TargetY = (int)(pTarget->Object.Position[1] / TERRAIN_SCALE);

        PATH_t tempPath;
        const bool bHasPath = PathFinding2(Hero->PositionX, Hero->PositionY, TargetX, TargetY, &tempPath, m_iHuntingDistance + fRange);
        if (!bHasPath)
        {
            DeleteTarget(iTarget);
            return 0;
        }

        const bool bTargetNear = CheckTile(Hero, &Hero->Object, fRange);
        const bool bNoWall = CheckWall(Hero->PositionX, Hero->PositionY, TargetX, TargetY);

        // Out of range or wall in the way -- walk a few steps closer this tick.
        if (!bTargetNear || !bNoWall)
        {
            Hero->Path.Lock.lock();
            const int pathNum = std::min<int>(tempPath.PathNum, 2);
            for (int i = 0; i < pathNum; i++)
            {
                Hero->Path.PathX[i] = tempPath.PathX[i];
                Hero->Path.PathY[i] = tempPath.PathY[i];
            }
            Hero->Path.PathNum = pathNum;
            Hero->Path.CurrentPath = 0;
            Hero->Path.CurrentPathFloat = 0;
            Hero->Path.Lock.unlock();

            SendMove(Hero, &Hero->Object);
            GameLogic::Helper::SessionStats::RecordMovementAttempt(Hero->PositionX, Hero->PositionY);
            return 0;
        }

        // In range -- replicate the main-loop basic-attack handoff
        // (ZzzInterface.cpp:7966-8000). Action() with MOVEMENT_ATTACK sends
        // the SendHitRequest packet and plays the swing animation.
        // Throttle to character AttackSpeed so we don't spam hits faster
        // than the swing animation cadence.
        const DWORD nowTick = GetTickCount();
        if (nowTick - m_dwLastBasicHitTick < static_cast<DWORD>(GetBasicAttackIntervalMs()))
        {
            return 1;
        }
        Hero->MovementType = MOVEMENT_ATTACK;
        ActionTarget = iCharIndex;
        Attacking = 1;
        Action(Hero, &Hero->Object, true);
        GameLogic::Helper::SessionStats::RecordActivity();
        m_dwLastBasicHitTick = nowTick;
        return 1;
    }

    int CMuHelper::Regroup()
    {
        if (m_config.bReturnToOriginalPosition && m_iSecondsAway > m_config.iMaxSecondsAway)
        {
            if (!SimulateMove(m_posOriginal))
            {
                return 0;
            }

            m_iSecondsAway = 0;
            m_iComboState = 0;
            m_iCurrentTarget = -1;
        }

        return 1;
    }

    int CMuHelper::SimulateMove(POINT posMove)
    {
        Hero->MovementType = MOVEMENT_MOVE;
        TargetX = (int)posMove.x;
        TargetY = (int)posMove.y;

        if (!CheckTile(Hero, &Hero->Object, 1.5f))
        {
            if (PathFinding2((Hero->PositionX), (Hero->PositionY), TargetX, TargetY, &Hero->Path))
            {
                SendMove(Hero, &Hero->Object);
                GameLogic::Helper::SessionStats::RecordMovementAttempt(Hero->PositionX, Hero->PositionY);
            }
            return 0;
        }

        return 1;
    }

    bool CMuHelper::HasAssignedBuffSkill()
    {
        for (int i = 0; i < m_config.aiBuff.size(); i++)
        {
            if (m_config.aiBuff[i] != 0)
            {
                return true;
            }
        }

        return false;
    }

    ActionSkillType CMuHelper::GetHealingSkill()
    {
        std::vector<ActionSkillType> aiHealingSkills =
        {
            AT_SKILL_HEALING,
            AT_SKILL_HEALING_STR,
        };

        for (int i = 0; i < aiHealingSkills.size(); i++)
        {
            int iSkillIndex = g_pSkillList->GetSkillIndex(aiHealingSkills[i]);
            if (iSkillIndex != -1)
            {
                return aiHealingSkills[i];
            }
        }

        return AT_SKILL_UNDEFINED;
    }

    // Matches AttackWizard() behavior in ZzzInterface.cpp for these skill IDs.
    bool CMuHelper::IsSelfPositionSkill(ActionSkillType iSkill)
    {
        return (
            iSkill == AT_SKILL_NOVA_BEGIN ||
            iSkill == AT_SKILL_NOVA ||
            iSkill == AT_SKILL_HELL_FIRE ||
            iSkill == AT_SKILL_HELL_FIRE_STR ||
            iSkill == AT_SKILL_INFERNO ||
            iSkill == AT_SKILL_INFERNO_STR ||
            iSkill == AT_SKILL_INFERNO_STR_MG
        );
    }

    ActionSkillType CMuHelper::GetDrainLifeSkill()
    {
        std::vector<ActionSkillType> aiDrainLifeSkills =
        {
            AT_SKILL_ALICE_DRAINLIFE,
            AT_SKILL_ALICE_DRAINLIFE_STR
        };

        for (int i = 0; i < aiDrainLifeSkills.size(); i++)
        {
            int iSkillIndex = g_pSkillList->GetSkillIndex(aiDrainLifeSkills[i]);
            if (iSkillIndex != -1)
            {
                return aiDrainLifeSkills[i];
            }
        }

        return AT_SKILL_UNDEFINED;
    }

    int CMuHelper::ObtainItem()
    {
        // Stuck-on-pickup recovery (helper used to idle forever while a mob
        // stood on the drop). WorkLoop ticks at ~5 Hz so 15 ticks ~= 3s.
        constexpr int MAX_OBTAIN_STUCK_TICKS = 15;

        if (m_iCurrentItem == MAX_ITEMS)
        {
            m_iCurrentItem = SelectItemToObtain();
            if (m_iCurrentItem == MAX_ITEMS)
            {
                m_iLastObtainItem = MAX_ITEMS;
                m_iObtainStuckTicks = 0;
                return 1;
            }
        }

        if (m_iCurrentItem != m_iLastObtainItem)
        {
            m_iLastObtainItem = m_iCurrentItem;
            m_iObtainStuckTicks = 0;
        }

        ITEM_t* pDrop = &Items[m_iCurrentItem];

        if (!pDrop->Object.Live)
        {
            DeleteItem(m_iCurrentItem);
            return 1;
        }

        TargetX = (int)(Items[m_iCurrentItem].Object.Position[0] / TERRAIN_SCALE);
        TargetY = (int)(Items[m_iCurrentItem].Object.Position[1] / TERRAIN_SCALE);

        int iDistance = ComputeDistanceBetween({ Hero->PositionX, Hero->PositionY }, { TargetX, TargetY });
        if (iDistance <= m_iObtainingDistance)
        {
            // Pickup tolerance when a mob occupies the drop tile. The strict
            // CheckTile (1.5) demands we stand right next to it, but the
            // server accepts pickups from ~2.5 tiles away, and PathFinding2
            // can't route us into a tile a monster is standing on. Try the
            // request anyway when we're already within the server's reach.
            constexpr float BLOCKED_PICKUP_RANGE = 2.5f;

            if (!CheckTile(Hero, &Hero->Object, 1.5f))
            {
                if (IsMonsterOnTile(TargetX, TargetY))
                {
                    // Close enough for the server -- send pickup despite the
                    // mob blocking the exact tile. This is what handles "many
                    // mobs, most drops covered" packs.
                    if (CheckTile(Hero, &Hero->Object, BLOCKED_PICKUP_RANGE))
                    {
                        if (SendGetItem == -1)
                        {
                            SendGetItem = m_iCurrentItem;
                            SocketClient->ToGameServer()->SendPickupItemRequest(m_iCurrentItem);
                            DeleteItem(m_iCurrentItem);
                        }
                        return 1;
                    }

                    // Still too far. Defer without blacklisting so Attack()
                    // runs this tick to clear the blocker, and the next round
                    // can either kill it or close the gap.
                    m_iCurrentItem = MAX_ITEMS;
                    m_iLastObtainItem = MAX_ITEMS;
                    m_iObtainStuckTicks = 0;
                    return 1;
                }

                const bool bHasPath = PathFinding2((Hero->PositionX), (Hero->PositionY), TargetX, TargetY, &Hero->Path);
                if (bHasPath)
                {
                    SendMove(Hero, &Hero->Object);
                }

                ++m_iObtainStuckTicks;
                // Hard skip: no path at all, or we've been trying too long.
                // Add to the session skip-set so SelectItemToObtain stops
                // returning it. DeleteItem clears the entry once the drop
                // disappears from the world.
                if (!bHasPath || m_iObtainStuckTicks >= MAX_OBTAIN_STUCK_TICKS)
                {
                    m_setSkippedItems.insert(m_iCurrentItem);
                    m_iCurrentItem = MAX_ITEMS;
                    m_iLastObtainItem = MAX_ITEMS;
                    m_iObtainStuckTicks = 0;
                    return 1;
                }

                return 0;
            }
            else
            {
                if (SendGetItem == -1)
                {
                    SendGetItem = m_iCurrentItem;
                    SocketClient->ToGameServer()->SendPickupItemRequest(m_iCurrentItem);
                    DeleteItem(m_iCurrentItem);
                }
            }
        }

        return 1;
    }

    bool CMuHelper::IsMonsterOnTile(int iTileX, int iTileY)
    {
        for (int i = 0; i < MAX_CHARACTERS_CLIENT; i++)
        {
            CHARACTER* p = &CharactersClient[i];
            if (!p->Object.Live || p->Dead > 0)
            {
                continue;
            }
            if (!IsMonster(p))
            {
                continue;
            }
            if (p->PositionX == iTileX && p->PositionY == iTileY)
            {
                return true;
            }
        }
        return false;
    }

    bool CMuHelper::ShouldObtainItem(int iItemId, bool bZenAllowed)
    {
        ITEM_t* pDrop = &Items[iItemId];
        ITEM* pItem = &pDrop->Item;

        if (!MatchesPickupFilters(pItem)) return false;
        if (IsMoneyItem(pItem))
        {
            // bPickZen-driven zen pickup is conditional: only when zen has
            // piled up faster than the bot is grinding it out, see
            // UpdateZenCleanupMode. zen matched via bPickAllItems falls
            // through unconditionally and keeps the prior behavior.
            if (m_config.bPickZen) return bZenAllowed;
            return true;
        }
        if (g_pMyInventory == nullptr) return true;     // UI not ready, let server decide
        return g_pMyInventory->CanFitItem(pItem);
    }

    bool CMuHelper::MatchesPickupFilters(ITEM* pItem)
    {
        if ((m_config.bPickZen && IsMoneyItem(pItem))
            || (m_config.bPickJewel && IsJewelItem(pItem))
            || (m_config.bPickAncient && IsAncientItem(pItem))
            || (m_config.bPickExcellent && IsExcellentItem(pItem)))
        {
            return true;
        }

        if (m_config.bPickExtraItems)
        {
            std::wstring strDisplayName = GetItemDisplayName(pItem);

            for (const auto& str : m_config.aExtraItems)
            {
                // Check if the search keyword is in the item's display name
                if (strDisplayName.find(str) != std::wstring::npos)
                {
                    return true;
                }
            }
        }

        return m_config.bPickAllItems;
    }

    void CMuHelper::AddItem(int iItemId, POINT posWhere)
    {
        _itemsLock.lock();
        if (m_setOwnDropItems.count(iItemId) > 0)
        {
            _itemsLock.unlock();
            return;
        }
        if (ClaimOwnDropAt(posWhere.x, posWhere.y))
        {
            m_setOwnDropItems.insert(iItemId);
            _itemsLock.unlock();
            return;
        }
        m_setItems.insert(iItemId);
        _itemsLock.unlock();
    }

    // Zen cleanup mode: bPickZen is conditional on zen actually piling up.
    // Counts zen piles vs monsters within the configured obtaining range.
    // The flag is sticky -- once tripped, stay in cleanup mode until every
    // visible zen pile is gone, so the bot doesn't oscillate as it picks them
    // up one by one. iObtainingRange = 0 means "no range configured" and the
    // mode stays off (consistent with the user opting out of distance-based
    // pickup).
    void CMuHelper::UpdateZenCleanupMode(const std::set<int>& itemIds)
    {
        int zenCount = 0;
        for (const int& iItemId : itemIds)
        {
            ITEM_t* pDrop = &Items[iItemId];
            if (!IsMoneyItem(&pDrop->Item)) continue;
            const int itemX = (int)(pDrop->Object.Position[0] / TERRAIN_SCALE);
            const int itemY = (int)(pDrop->Object.Position[1] / TERRAIN_SCALE);
            const int dist = ComputeDistanceBetween({ Hero->PositionX, Hero->PositionY }, { itemX, itemY });
            if (dist <= m_iObtainingDistance) ++zenCount;
        }

        if (zenCount == 0)
        {
            m_bZenCleanupMode = false;
            return;
        }

        std::set<int> setTargets;
        {
            _targetsLock.lock();
            setTargets = m_setTargets;
            _targetsLock.unlock();
        }

        int monsterCount = 0;
        for (const int& iTargetId : setTargets)
        {
            const int iIndex = FindCharacterIndex(iTargetId);
            if (iIndex == MAX_CHARACTERS_CLIENT) continue;
            CHARACTER* pTarget = &CharactersClient[iIndex];
            if (!IsMonster(pTarget) || pTarget->Dead != 0) continue;
            const int dist = ComputeDistanceBetween({ Hero->PositionX, Hero->PositionY }, { pTarget->PositionX, pTarget->PositionY });
            if (dist <= m_iObtainingDistance) ++monsterCount;
        }

        if (zenCount > monsterCount)
        {
            m_bZenCleanupMode = true;
        }
    }

    // Own-drop tracking: if the player just threw something away, the bot
    // should leave it on the floor. Drop sites call NoteOwnDrop(tx, ty) right
    // before SendDropItemRequest. When the server echoes the drop back via
    // ReceiveCreateItemViewportExtended / ReceiveCreateMoney, AddItem matches
    // the incoming floor item against the recorded tiles (within
    // OWN_DROP_TILE_TOLERANCE to absorb server-side placement shifts and within
    // OWN_DROP_TTL_MS to bound staleness) and parks the id in
    // m_setOwnDropItems instead of m_setItems -- the bot never sees it.
    // Important: we deliberately do NOT route through m_setSkippedItems,
    // because DeleteTarget clears that set on every mob death and would
    // un-skip our drop within seconds of any kill.
    void CMuHelper::NoteOwnDrop(int tx, int ty)
    {
        const DWORD nowTick = GetTickCount();
        _itemsLock.lock();

        int oldestSlot = 0;
        DWORD oldestTick = nowTick;
        for (int i = 0; i < kMaxOwnDrops; i++)
        {
            const bool bExpired = (nowTick - m_aOwnDrops[i].tickRecorded) > OWN_DROP_TTL_MS;
            if (m_aOwnDrops[i].x < 0 || bExpired)
            {
                m_aOwnDrops[i] = { tx, ty, nowTick };
                _itemsLock.unlock();
                return;
            }
            if (m_aOwnDrops[i].tickRecorded < oldestTick)
            {
                oldestTick = m_aOwnDrops[i].tickRecorded;
                oldestSlot = i;
            }
        }

        m_aOwnDrops[oldestSlot] = { tx, ty, nowTick };
        _itemsLock.unlock();
    }

    bool CMuHelper::ClaimOwnDropAt(int tx, int ty)
    {
        const DWORD nowTick = GetTickCount();
        for (int i = 0; i < kMaxOwnDrops; i++)
        {
            if (m_aOwnDrops[i].x < 0) continue;
            const int dx = m_aOwnDrops[i].x - tx;
            const int dy = m_aOwnDrops[i].y - ty;
            if (dx < -OWN_DROP_TILE_TOLERANCE || dx > OWN_DROP_TILE_TOLERANCE) continue;
            if (dy < -OWN_DROP_TILE_TOLERANCE || dy > OWN_DROP_TILE_TOLERANCE) continue;
            if ((nowTick - m_aOwnDrops[i].tickRecorded) > OWN_DROP_TTL_MS) continue;
            m_aOwnDrops[i] = {};
            return true;
        }
        return false;
    }

    void CMuHelper::DeleteItem(int iItemId)
    {
        _itemsLock.lock();
        m_setItems.erase(iItemId);
        m_setOwnDropItems.erase(iItemId);
        _itemsLock.unlock();

        m_setSkippedItems.erase(iItemId);

        if (iItemId == m_iCurrentItem)
        {
            m_iCurrentItem = MAX_ITEMS;
            m_iLastObtainItem = MAX_ITEMS;
            m_iObtainStuckTicks = 0;
        }
    }

    int CMuHelper::SelectItemToObtain()
    {
        int iClosestItemId = MAX_ITEMS;
        int iMinDistance = m_config.iObtainingRange;

        std::set<int> setItems;
        {
            _itemsLock.lock();
            setItems = m_setItems;
            _itemsLock.unlock();
        }

        UpdateZenCleanupMode(setItems);
        const bool bZenAllowed = m_bZenCleanupMode;

        for (const int& iItemId : setItems)
        {
            if (m_setSkippedItems.count(iItemId) > 0)
            {
                continue;
            }

            if (!ShouldObtainItem(iItemId, bZenAllowed))
            {
                continue;
            }

            int iItemX = (int)(Items[iItemId].Object.Position[0] / TERRAIN_SCALE);
            int iItemY = (int)(Items[iItemId].Object.Position[1] / TERRAIN_SCALE);

            int iDistance = ComputeDistanceBetween({ Hero->PositionX, Hero->PositionY }, { iItemX, iItemY });
            if (iDistance <= iMinDistance)
            {
                iMinDistance = iDistance;
                iClosestItemId = iItemId;
            }
        }

        return iClosestItemId;
    }
}
