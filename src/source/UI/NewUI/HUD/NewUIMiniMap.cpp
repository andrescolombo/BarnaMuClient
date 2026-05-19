// NewUIGuildInfoWindow.cpp: implementation of the CNewUIGuildInfoWindow class.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "I18N/All.h"

#include "UI/NewUI/HUD/NewUIMiniMap.h"
#include "UI/NewUI/NewUISystem.h"
#include "UI/NewUI/Dialogs/NewUICommonMessageBox.h"
#include "UI/NewUI/Dialogs/NewUICustomMessageBox.h"
#include "Audio/DSPlaySound.h"

#include "Guild/NewUIGuildInfoWindow.h"
#include "UI/NewUI/Widgets/NewUIButton.h"
#include "UI/NewUI/Inventory/NewUIMyInventory.h"
#include "GameLogic/Items/CSItemOption.h"
#include "World/MapInfra/MapManager.h"
#include "Engine/AI/ZzzAI.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Engine/Object/ZzzInterface.h"
#include "Render/Effects/ZzzEffect.h"
#include "Render/Terrain/ZzzLodTerrain.h"

extern BYTE m_OccupationState;
extern int TargetX;
extern int TargetY;

using namespace SEASON3B;

namespace
{
    constexpr int MINIMAP_VIEWPORT_X = 0;
    constexpr int MINIMAP_VIEWPORT_Y = 0;
    constexpr int MINIMAP_VIEWPORT_WIDTH = REFERENCE_WIDTH;
    constexpr int MINIMAP_VIEWPORT_HEIGHT = 430;
    constexpr int MINIMAP_TILE_MIN = 0;
    constexpr int MINIMAP_TILE_MAX = TERRAIN_SIZE - 1;
    constexpr int MINIMAP_ZOOM_LEVEL_COUNT = 6;
    constexpr float MINIMAP_PLAYER_CENTER_X = static_cast<float>(REFERENCE_WIDTH) * 0.5f;
    constexpr float MINIMAP_PLAYER_CENTER_Y = static_cast<float>(REFERENCE_HEIGHT) * 0.5f;
    constexpr float MINIMAP_INVERSE_ROTATION_FACTOR = 0.70710678f;
    constexpr float MINIMAP_TILE_COUNT = static_cast<float>(TERRAIN_SIZE);
    constexpr float MINIMAP_TARGET_EFFECT_SCALE = 0.6f;
    constexpr float MINIMAP_TARGET_LIGHT = 1.0f;

    int ClampMinimapTile(int value)
    {
        if (value < MINIMAP_TILE_MIN)
        {
            return MINIMAP_TILE_MIN;
        }
        if (value > MINIMAP_TILE_MAX)
        {
            return MINIMAP_TILE_MAX;
        }

        return value;
    }

    bool IsMinimapTileWalkable(int tileX, int tileY)
    {
        const WORD attribute = TerrainWall[TERRAIN_INDEX(tileX, tileY)];
        return attribute < TW_NOGROUND
            || (attribute & TW_ACTION) == TW_ACTION
            || (attribute & TW_HEIGHT) == TW_HEIGHT;
    }

    void CreateMinimapMoveTargetEffect(int tileX, int tileY)
    {
        OBJECT* pHeroObj = &Hero->Object;
        vec3_t targetPosition;
        vec3_t targetLight;

        targetPosition[0] = static_cast<float>(tileX) * TERRAIN_SCALE + (TERRAIN_SCALE * 0.5f);
        targetPosition[1] = static_cast<float>(tileY) * TERRAIN_SCALE + (TERRAIN_SCALE * 0.5f);
        targetPosition[2] = RequestTerrainHeight(targetPosition[0], targetPosition[1]);

        Vector(MINIMAP_TARGET_LIGHT, MINIMAP_TARGET_LIGHT, 0.0f, targetLight);
        DeleteEffect(MODEL_MOVE_TARGETPOSITION_EFFECT);

        if ((TerrainWall[TERRAIN_INDEX(tileX, tileY)] & TW_NOMOVE) != TW_NOMOVE)
        {
            CreateEffect(MODEL_MOVE_TARGETPOSITION_EFFECT, targetPosition, pHeroObj->Angle, targetLight,
                0, pHeroObj, -1, 0, 0, 0, MINIMAP_TARGET_EFFECT_SCALE);
        }
    }
}

SEASON3B::CNewUIMiniMap::CNewUIMiniMap()
{
    m_pNewUIMng = NULL;
    m_HasMoveTarget = false;
    m_MoveTargetX = 0;
    m_MoveTargetY = 0;
}

SEASON3B::CNewUIMiniMap::~CNewUIMiniMap()
{
    Release();
}

bool SEASON3B::CNewUIMiniMap::Create(CNewUIManager* pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MINI_MAP, this);

    LoadBitmap(L"Interface\\mini_map_ui_corner.tga", IMAGE_MINIMAP_INTERFACE + 1, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_line.jpg", IMAGE_MINIMAP_INTERFACE + 2, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_cha.tga", IMAGE_MINIMAP_INTERFACE + 3, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_portal.tga", IMAGE_MINIMAP_INTERFACE + 4, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_npc.tga", IMAGE_MINIMAP_INTERFACE + 5, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_cancel.tga", IMAGE_MINIMAP_INTERFACE + 6, GL_LINEAR);

    m_BtnExit.ChangeButtonImgState(true, IMAGE_MINIMAP_INTERFACE + 6, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 610, 3, 85, 85);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);	// 1002 "�ݱ�"

    SetPos(x, y);

    m_Lenth[0].x = 800;
    m_Lenth[1].x = 1000;
    m_Lenth[2].x = 1200;
    m_Lenth[3].x = 1400;
    m_Lenth[4].x = 1600;
    m_Lenth[5].x = 1800;
    m_Lenth[0].y = 800;
    m_Lenth[1].y = 1000;
    m_Lenth[2].y = 1200;
    m_Lenth[3].y = 1400;
    m_Lenth[4].y = 1600;
    m_Lenth[5].y = 1800;
    m_MiniPos = 0;
    m_bSuccess = false;
    return true;
}

void SEASON3B::CNewUIMiniMap::ClosingProcess()
{
    ResetMoveTarget();
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float SEASON3B::CNewUIMiniMap::GetLayerDepth()
{
    return 8.1f;
}

void SEASON3B::CNewUIMiniMap::OpenningProcess()
{
}

void SEASON3B::CNewUIMiniMap::Release()
{
    UnloadImages();

    for (int i = 1; i < 7; i++)
    {
        DeleteBitmap(IMAGE_MINIMAP_INTERFACE + i);
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUIMiniMap::SetPos(int x, int y)
{
    m_BtnExit.ChangeButtonInfo(REFERENCE_WIDTH - 27, 3, 30, 25);
}

void SEASON3B::CNewUIMiniMap::SetBtnPos(int Num, float x, float y, float nx, float ny)
{
    m_Btn_Loc[Num][0] = x;
    m_Btn_Loc[Num][1] = y;
    m_Btn_Loc[Num][2] = nx;
    m_Btn_Loc[Num][3] = ny;
}

bool SEASON3B::CNewUIMiniMap::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MINI_MAP))
    {
        if (IsPress(VK_ESCAPE) == true || IsPress(VK_TAB) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool SEASON3B::CNewUIMiniMap::Render()
{
    float Rot = 45.f;

    if (m_bSuccess == false)
        return m_bSuccess;

    EnableAlphaTest();
    RenderColor(0, 0, REFERENCE_WIDTH, 430, 0.85f, 1);
    DisableAlphaBlend();
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    auto Ty = (float)(((float)Hero->PositionX / 256.f) * m_Lenth[m_MiniPos].y);
    auto Tx = (float)(((float)Hero->PositionY / 256.f) * m_Lenth[m_MiniPos].x);
    float Ty1;
    float Tx1;
    float uvxy = (41.7f / 64.f);
    float uvxy_Line = 8.f / 8.f;
    float Ui_wid = 35.f;
    float Ui_Hig = 6.f;
    float Rot_Loc = 45.f;
    int i = 0;

    RenderBitRotate(IMAGE_MINIMAP_INTERFACE, m_Lenth[m_MiniPos].x - Tx, m_Lenth[m_MiniPos].y - Ty, m_Lenth[m_MiniPos].x, m_Lenth[m_MiniPos].y, Rot);

    int NpcWidth = 15;
    int NpcWidthP = 30;
    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        if (m_Mini_Map_Data[i].Kind > 0)
        {
            Ty1 = (float)(((float)m_Mini_Map_Data[i].Location[0] / 256.f) * m_Lenth[m_MiniPos].y);
            Tx1 = (float)(((float)m_Mini_Map_Data[i].Location[1] / 256.f) * m_Lenth[m_MiniPos].x);
            Rot_Loc = (float)m_Mini_Map_Data[i].Rotation;

            if (m_Mini_Map_Data[i].Kind == 1) //npc
            {
                if (!(gMapManager.WorldActive == WD_34CRYWOLF_1ST && m_OccupationState > 0) || (m_Mini_Map_Data[i].Location[0] == 228 && m_Mini_Map_Data[i].Location[1] == 48 && gMapManager.WorldActive == WD_34CRYWOLF_1ST))
                    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 5, Tx1, Ty1, NpcWidth, NpcWidth, m_Lenth[m_MiniPos].x - Tx, m_Lenth[m_MiniPos].y - Ty, m_Lenth[m_MiniPos].x, m_Lenth[m_MiniPos].y, Rot, Rot_Loc, 17.5f / 32.f, 17.5f / 32.f, i);
            }
            else
                if (m_Mini_Map_Data[i].Kind == 2)
                    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 4, Tx1, Ty1, NpcWidthP, NpcWidthP, m_Lenth[m_MiniPos].x - Tx, m_Lenth[m_MiniPos].y - Ty, m_Lenth[m_MiniPos].x, m_Lenth[m_MiniPos].y, Rot, Rot_Loc, 17.5f / 32.f, 17.5f / 32.f, 100 + i);
        }
        else
            break;
    }

    float Ch_wid = 12;
    RenderImage(IMAGE_MINIMAP_INTERFACE + 3, 325, 230, Ch_wid, Ch_wid, 0.f, 0.f, 17.5f / 32.f, 17.5f / 32.f);

    for (i = 0; i < 25; i++)
    {
        RenderImage(IMAGE_MINIMAP_INTERFACE + 2, i * Ui_wid, 0, Ui_wid, Ui_Hig, 0.f, 1.f, uvxy, -uvxy_Line);
        RenderImage(IMAGE_MINIMAP_INTERFACE + 2, i * Ui_wid, 430 - Ui_Hig, Ui_wid, Ui_Hig, 0.f, 0.f, uvxy, uvxy_Line);
    }
    for (i = 0; i < 20; i++)
    {
        RenderBitmapRotate(IMAGE_MINIMAP_INTERFACE + 2, (Ui_Hig / 2.f), i * (Ui_wid - 3.f), Ui_wid, Ui_Hig, -90.f, 0.f, 0.f, uvxy, uvxy_Line);
        RenderBitmapRotate(IMAGE_MINIMAP_INTERFACE + 2, REFERENCE_WIDTH - (Ui_Hig / 2.f), i * (Ui_wid - 3.f), Ui_wid, Ui_Hig, 90.f, 0.f, 0.f, uvxy, uvxy_Line);
    }

    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 0, 0, Ui_wid, Ui_wid, 0.f, 0.f, uvxy, uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, REFERENCE_WIDTH - Ui_wid, 0, Ui_wid, Ui_wid, uvxy, 0.f, -uvxy, uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 0, 430 - Ui_wid, Ui_wid, Ui_wid, 0.f, uvxy, uvxy, -uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, REFERENCE_WIDTH - Ui_wid, 430 - Ui_wid, Ui_wid, Ui_wid, uvxy, uvxy, -uvxy, -uvxy);

    m_BtnExit.Render(true);

    DisableAlphaBlend();

    Check_Btn(MouseX, MouseY);
    return true;
}

bool SEASON3B::CNewUIMiniMap::Update()
{
    ContinueMoveTarget();
    return true;
}

void SEASON3B::CNewUIMiniMap::LoadImages(const wchar_t* Filename)
{
    wchar_t Fname[300];
    int i = 0;
    mu_swprintf(Fname, L"Data\\%ls\\mini_map.ozt", Filename);
    FILE* pFile = _wfopen(Fname, L"rb");

    if (pFile == NULL)
    {
        m_bSuccess = false;
        return;
    }
    else
    {
        m_bSuccess = true;
        fclose(pFile);
        mu_swprintf(Fname, L"%ls\\mini_map.tga", Filename);
        LoadBitmap(Fname, IMAGE_MINIMAP_INTERFACE, GL_LINEAR);
    }

    mu_swprintf(Fname, L"Data\\Local\\%ls\\Minimap\\Minimap_%ls_%ls.bmd", g_strSelectedML.c_str(), Filename, g_strSelectedML.c_str());

    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        m_Mini_Map_Data[i].Kind = 0;
    }

    FILE* fp = _wfopen(Fname, L"rb");

    if (fp != NULL)
    {
        int Size = sizeof(MINI_MAP_FILE);
        BYTE* Buffer = new BYTE[Size * MAX_MINI_MAP_DATA + 45];
        fread(Buffer, (Size * MAX_MINI_MAP_DATA) + 45, 1, fp);

        DWORD dwCheckSum;
        fread(&dwCheckSum, sizeof(DWORD), 1, fp);
        fclose(fp);

        if (dwCheckSum != GenerateCheckSum2(Buffer, (Size * MAX_MINI_MAP_DATA) + 45, 0x2BC1))
        {
            wchar_t Text[256];
            mu_swprintf(Text, L"%ls - File corrupted.", Fname);
            g_ErrorReport.Write(Text);
            MessageBox(g_hWnd, Text, NULL, MB_OK);
            SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        }
        else
        {
            BYTE* pSeek = Buffer;

            for (i = 0; i < MAX_MINI_MAP_DATA; i++)
            {
                BuxConvert(pSeek, Size);
                //memcpy(&(m_Mini_Map_Data[i]), pSeek, Size);

                MINI_MAP_FILE current{ };
                auto target = &(m_Mini_Map_Data[i]);
                memcpy(&current, pSeek, Size);
                memcpy(target, pSeek, Size);

                CMultiLanguage::ConvertFromUtf8(target->Name, current.Name);
                /*int wchars_num = MultiByteToWideChar(CP_UTF8, 0, current.Name, -1, NULL, 0);
                MultiByteToWideChar(CP_UTF8, 0, current.Name, -1, target->Name, wchars_num);
                target->Name[wchars_num] = L'\0';*/
                pSeek += Size;
            }
        }

        delete[] Buffer;
    }
}

void SEASON3B::CNewUIMiniMap::UnloadImages()
{
    DeleteBitmap(IMAGE_MINIMAP_INTERFACE);
}

bool SEASON3B::CNewUIMiniMap::UpdateMouseEvent()
{
    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);
        return true;
    }

    if (IsPress(VK_LBUTTON) &&
        CheckMouseIn(MINIMAP_VIEWPORT_X, MINIMAP_VIEWPORT_Y, MINIMAP_VIEWPORT_WIDTH, MINIMAP_VIEWPORT_HEIGHT))
    {
        if (Check_Mouse(MouseX, MouseY))
        {
            PlayBuffer(SOUND_CLICK01);
        }

        return false;
    }

    if (CheckMouseIn(MINIMAP_VIEWPORT_X, MINIMAP_VIEWPORT_Y, MINIMAP_VIEWPORT_WIDTH, MINIMAP_VIEWPORT_HEIGHT))
    {
        return false;
    }

    return true;
}

void SEASON3B::CNewUIMiniMap::ResetMoveTarget()
{
    m_HasMoveTarget = false;
    m_MoveTargetX = 0;
    m_MoveTargetY = 0;
}

bool SEASON3B::CNewUIMiniMap::TrySendMoveSegment(int targetTileX, int targetTileY)
{
    if (Hero == nullptr)
    {
        return false;
    }
    if (Hero->PositionX == targetTileX && Hero->PositionY == targetTileY)
    {
        return false;
    }
    if (!PathFinding2(Hero->PositionX, Hero->PositionY, targetTileX, targetTileY, &Hero->Path))
    {
        return false;
    }

    TargetX = targetTileX;
    TargetY = targetTileY;
    Hero->MovementType = MOVEMENT_MOVE;
    SendMove(Hero, &Hero->Object);
    return true;
}

void SEASON3B::CNewUIMiniMap::ContinueMoveTarget()
{
    if (!m_HasMoveTarget)
    {
        return;
    }
    if (Hero == nullptr)
    {
        ResetMoveTarget();
        return;
    }
    if (Hero->PositionX == m_MoveTargetX && Hero->PositionY == m_MoveTargetY)
    {
        ResetMoveTarget();
        return;
    }
    if (Hero->Movement && Hero->Path.CurrentPath < Hero->Path.PathNum - 1)
    {
        return;
    }
    if (!TrySendMoveSegment(m_MoveTargetX, m_MoveTargetY))
    {
        ResetMoveTarget();
    }
}

bool SEASON3B::CNewUIMiniMap::Check_Mouse(int mx, int my)
{
    if (m_bSuccess == false || Hero == nullptr)
    {
        return false;
    }
    if (m_MiniPos < 0 || m_MiniPos >= MINIMAP_ZOOM_LEVEL_COUNT)
    {
        return false;
    }

    const float zoomWidth = static_cast<float>(m_Lenth[m_MiniPos].x);
    const float zoomHeight = static_cast<float>(m_Lenth[m_MiniPos].y);
    if (zoomWidth <= 0.0f || zoomHeight <= 0.0f)
    {
        return false;
    }

    const float screenOffsetX = static_cast<float>(mx) - MINIMAP_PLAYER_CENTER_X;
    const float screenOffsetY = MINIMAP_PLAYER_CENTER_Y - static_cast<float>(my);
    const float mapHorizontalOffset = (screenOffsetX + screenOffsetY) * MINIMAP_INVERSE_ROTATION_FACTOR;
    const float mapVerticalOffset = (-screenOffsetX + screenOffsetY) * MINIMAP_INVERSE_ROTATION_FACTOR;

    const int targetTileY = ClampMinimapTile(static_cast<int>(
        Hero->PositionY + mapHorizontalOffset * (MINIMAP_TILE_COUNT / zoomWidth)));
    const int targetTileX = ClampMinimapTile(static_cast<int>(
        Hero->PositionX - mapVerticalOffset * (MINIMAP_TILE_COUNT / zoomHeight)));
    if (!IsMinimapTileWalkable(targetTileX, targetTileY))
    {
        return false;
    }
    if (!TrySendMoveSegment(targetTileX, targetTileY))
    {
        return false;
    }

    m_HasMoveTarget = true;
    m_MoveTargetX = targetTileX;
    m_MoveTargetY = targetTileY;
    CreateMinimapMoveTargetEffect(targetTileX, targetTileY);
    return true;
}

bool SEASON3B::CNewUIMiniMap::Check_Btn(int mx, int my)
{
    int i = 0;
    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        if (m_Mini_Map_Data[i].Kind > 0)
        {
            if (mx > m_Btn_Loc[i][0] && mx < (m_Btn_Loc[i][0] + m_Btn_Loc[i][2]) && my > m_Btn_Loc[i][1] && my < (m_Btn_Loc[i][1] + m_Btn_Loc[i][3]))
            {
                SIZE Fontsize;
                m_TooltipText = (std::wstring)m_Mini_Map_Data[i].Name;
                g_pRenderText->SetFont(g_hFont);
                GetTextExtentPoint32(g_pRenderText->GetFontDC(), m_TooltipText.c_str(), m_TooltipText.size(), &Fontsize);

                Fontsize.cx = Fontsize.cx / ((float)WindowWidth / REFERENCE_WIDTH);
                Fontsize.cy = Fontsize.cy / ((float)WindowHeight / REFERENCE_HEIGHT);

                int x = m_Btn_Loc[i][0] + ((m_Btn_Loc[i][2] / 2) - (Fontsize.cx / 2));
                int y = m_Btn_Loc[i][1] + m_Btn_Loc[i][3] + 2;

                y = m_Btn_Loc[i][1] - (Fontsize.cy + 2);

                DWORD backuptextcolor = g_pRenderText->GetTextColor();
                DWORD backuptextbackcolor = g_pRenderText->GetBgColor();

                g_pRenderText->SetTextColor(RGBA(255, 255, 255, 255));
                g_pRenderText->SetBgColor(RGBA(0, 0, 0, 180));
                g_pRenderText->RenderText(x, y, m_TooltipText.c_str(), Fontsize.cx + 6, 0, RT3_SORT_CENTER);

                g_pRenderText->SetTextColor(backuptextcolor);
                g_pRenderText->SetBgColor(backuptextbackcolor);

                return true;
            }
        }
        else
            break;
    }
    return false;
}
