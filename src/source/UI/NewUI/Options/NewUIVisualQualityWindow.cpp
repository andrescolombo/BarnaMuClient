// NewUIVisualQualityWindow.cpp: Visual Quality options panel.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "UI/NewUI/Options/NewUIVisualQualityWindow.h"
#include "UI/NewUI/NewUISystem.h"
#include "Data/GameConfig/GameConfig.h"
#include "Render/Textures/ZzzOpenglUtil.h"
#include "Render/Sprites/GlobalBitmap.h"
#include "Audio/DSPlaySound.h"

using namespace SEASON3B;

// ---------------------------------------------------------------------------
// Layout constants (all relative to m_Pos)
// ---------------------------------------------------------------------------
namespace
{
    // Window dimensions
    constexpr int WIN_W = 190;
    constexpr int WIN_H = 269;  // 64 (top) + 16×10 (middle) + 45 (bottom)

    // Row Y-locals  (content starts after 64-px top image)
    constexpr int ROW_MIPMAP_Y     = 46;
    constexpr int ROW_ANISO_LBL_Y  = 78;
    constexpr int ROW_ANISO_CMB_Y  = 92;
    constexpr int ROW_VSYNC_Y      = 120;
    constexpr int ROW_MSAA_LBL_Y   = 148;
    constexpr int ROW_MSAA_CMB_Y   = 162;
    constexpr int ROW_NOTE_Y       = 194;
    constexpr int ROW_CLOSE_BTN_Y  = 224;

    // Checkbox hit region
    constexpr int CB_X_LOCAL = 150;
    constexpr int CB_SIZE    = 15;

    // Cycle-arrow value selector dimensions
    constexpr int COMBO_X_LOCAL = 20;
    constexpr int COMBO_W       = 150;
    constexpr int COMBO_H       = 16;
    constexpr int ARROW_W       = 16;
    constexpr int ARROW_H       = 16;

    // Separator lines
    constexpr int SEP_X_LOCAL = 18;
    constexpr int SEP_W       = 154;
    constexpr float SEP_H     = 2.f;

    // Point bullet X
    constexpr int POINT_X_LOCAL = 20;

    // Text left margin
    constexpr int TEXT_X_LOCAL = 40;

    // Anisotropy combo values (index → GL value)
    constexpr int ANISO_VALUES[] = { 0, 2, 4, 8, 16 };
    constexpr int ANISO_COUNT    = 5;

    // MSAA combo values (index → sample count)
    constexpr int MSAA_VALUES[] = { 0, 2, 4 };
    constexpr int MSAA_COUNT    = 3;

    // Labels — static storage required by CNewUIComboBox
    const wchar_t* const s_AnisoLabels[ANISO_COUNT] = {
        L"Off", L"2x", L"4x", L"8x", L"16x"
    };
    const wchar_t* const s_MSAALabels[MSAA_COUNT] = {
        L"Off", L"2x", L"4x"
    };
}

// Image indices reused from the Option window frame — the Option window loads
// these at startup and keeps them alive the entire game session.
enum VQ_IMAGE
{
    VQ_IMG_BACK    = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
    VQ_IMG_CLOSE   = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_CLOSE,
    VQ_IMG_TOP     = BITMAP_OPTION_BEGIN,
    VQ_IMG_LEFT    = BITMAP_OPTION_BEGIN + 1,
    VQ_IMG_RIGHT   = BITMAP_OPTION_BEGIN + 2,
    VQ_IMG_LINE    = BITMAP_OPTION_BEGIN + 3,
    VQ_IMG_POINT   = BITMAP_OPTION_BEGIN + 4,
    VQ_IMG_CHECK   = BITMAP_OPTION_BEGIN + 5,
    VQ_IMG_BOTTOM  = BITMAP_INTERFACE_NEW_PERSONALINVENTORY_BEGIN + 4, // same as IMAGE_INVENTORY_BACK_BOTTOM
};

// ---------------------------------------------------------------------------

CNewUIVisualQualityWindow::CNewUIVisualQualityWindow()
    : m_pNewUIMng(nullptr)
    , m_bMipmap(false)
    , m_bVSync(true)
    , m_iAnisotropyIdx(0)
    , m_iMSAAIdx(0)
{
    m_Pos.x = 0;
    m_Pos.y = 0;
}

CNewUIVisualQualityWindow::~CNewUIVisualQualityWindow()
{
    Release();
}

bool CNewUIVisualQualityWindow::Create(CNewUIManager* pNewUIMng, int x, int y)
{
    if (pNewUIMng == nullptr)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_VISUAL_QUALITY_OPTION, this);

    SetPos(x, y);
    LoadImages();
    SetButtonInfo();

    // Sync state from config
    const GameConfig& cfg = GameConfig::GetInstance();
    m_bMipmap        = cfg.GetMipmap();
    m_bVSync         = cfg.GetVSync();
    m_iAnisotropyIdx = AnisotropyValueToIndex(cfg.GetAnisotropy());
    m_iMSAAIdx       = MSAAValueToIndex(cfg.GetMSAA());

    Show(false);
    return true;
}

void CNewUIVisualQualityWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

void CNewUIVisualQualityWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

void CNewUIVisualQualityWindow::LoadImages()
{
    // Close button is managed by the message-box manager — nothing to load here.
    // The Option-frame images (VQ_IMG_TOP etc.) are loaded by CNewUIOptionWindow at
    // startup; they share bitmap indices so they remain live the whole session.
    // We only load the back image to increment its ref count safely.
    LoadBitmap(L"Interface\\newui_msgbox_back.jpg", VQ_IMG_BACK, GL_LINEAR);
}

void CNewUIVisualQualityWindow::UnloadImages()
{
    DeleteBitmap(VQ_IMG_BACK);
}

void CNewUIVisualQualityWindow::SetButtonInfo()
{
    m_BtnClose.ChangeTextBackColor(RGBA(255, 255, 255, 0));
    m_BtnClose.ChangeButtonImgState(true, VQ_IMG_CLOSE, true);
    m_BtnClose.ChangeButtonInfo(m_Pos.x + 68, m_Pos.y + ROW_CLOSE_BTN_Y, 54, 30);
    m_BtnClose.ChangeImgColor(BUTTON_STATE_UP,   RGBA(255, 255, 255, 255));
    m_BtnClose.ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

// ---------------------------------------------------------------------------

bool CNewUIVisualQualityWindow::UpdateMouseEvent()
{
    if (m_BtnClose.UpdateMouseEvent())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_VISUAL_QUALITY_OPTION);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }

    HandleCheckboxInput();

    // Consume all clicks inside the window area.
    if (CheckMouseIn(m_Pos.x, m_Pos.y, WIN_W, WIN_H))
        return false;

    return true;
}

bool CNewUIVisualQualityWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_VISUAL_QUALITY_OPTION))
    {
        if (SEASON3B::IsPress(VK_ESCAPE))
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_VISUAL_QUALITY_OPTION);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool CNewUIVisualQualityWindow::Update()
{
    return true;
}

bool CNewUIVisualQualityWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderContents();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

float CNewUIVisualQualityWindow::GetLayerDepth()
{
    return 10.5f;
}

float CNewUIVisualQualityWindow::GetKeyEventOrder()
{
    return 10.0f;
}

void CNewUIVisualQualityWindow::OpenningProcess()
{
    // Re-sync from config each time the panel opens.
    const GameConfig& cfg = GameConfig::GetInstance();
    m_bMipmap        = cfg.GetMipmap();
    m_bVSync         = cfg.GetVSync();
    m_iAnisotropyIdx = AnisotropyValueToIndex(cfg.GetAnisotropy());
    m_iMSAAIdx       = MSAAValueToIndex(cfg.GetMSAA());
}

void CNewUIVisualQualityWindow::ClosingProcess()
{
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void CNewUIVisualQualityWindow::RenderFrame()
{
    float x = static_cast<float>(m_Pos.x);
    float y = static_cast<float>(m_Pos.y);
    const float w = static_cast<float>(WIN_W);

    // Background
    RenderImage(VQ_IMG_BACK, x, y + 2.f, w - 6.f, static_cast<float>(WIN_H) - 6.f);

    // Top bar
    RenderImage(VQ_IMG_TOP, x, y, w, 64.f);

    // Middle slices (16 × 10 = 160 px)
    y += 64.f;
    for (int i = 0; i < 16; ++i)
    {
        RenderImage(VQ_IMG_LEFT,  x,           y, 21.f, 10.f);
        RenderImage(VQ_IMG_RIGHT, x + w - 21.f, y, 21.f, 10.f);
        y += 10.f;
    }

    // Bottom bar
    RenderImage(VQ_IMG_BOTTOM, x, y, w, 45.f);

    // Separator lines
    const float sx = static_cast<float>(m_Pos.x + SEP_X_LOCAL);
    auto renderSep = [&](int yLocal) {
        RenderImage(VQ_IMG_LINE, sx, static_cast<float>(m_Pos.y + yLocal), static_cast<float>(SEP_W), SEP_H);
    };

    renderSep(ROW_MIPMAP_Y    + 20);
    renderSep(ROW_ANISO_LBL_Y + 48);  // after aniso combo
    renderSep(ROW_VSYNC_Y     + 20);
    renderSep(ROW_MSAA_LBL_Y  + 50);  // after msaa combo
    renderSep(ROW_NOTE_Y      + 16);
}

void CNewUIVisualQualityWindow::RenderContents()
{
    const float px = static_cast<float>(m_Pos.x + POINT_X_LOCAL);
    const float tx = static_cast<float>(m_Pos.x + TEXT_X_LOCAL);
    const float cbx = static_cast<float>(m_Pos.x + CB_X_LOCAL);

    // --- Mipmap row ---
    float py = static_cast<float>(m_Pos.y + ROW_MIPMAP_Y);
    RenderImage(VQ_IMG_POINT, px, py, 10.f, 10.f);
    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetTextColor(255, 255, 255, 255);
    g_pRenderText->SetBgColor(0);
    g_pRenderText->RenderText(m_Pos.x + TEXT_X_LOCAL, m_Pos.y + ROW_MIPMAP_Y + 2, L"Mipmap");
    // Checkbox
    if (m_bMipmap)
        RenderImage(VQ_IMG_CHECK, cbx, py, 15.f, 15.f, 0, 0);
    else
        RenderImage(VQ_IMG_CHECK, cbx, py, 15.f, 15.f, 0, 15.f);

    // --- Anisotropy row ---
    py = static_cast<float>(m_Pos.y + ROW_ANISO_LBL_Y);
    RenderImage(VQ_IMG_POINT, px, py, 10.f, 10.f);
    g_pRenderText->RenderText(m_Pos.x + TEXT_X_LOCAL, m_Pos.y + ROW_ANISO_LBL_Y + 2, L"Anisotropy");
    // < value > selector
    {
        const int leftX  = m_Pos.x + COMBO_X_LOCAL;
        const int rightX = m_Pos.x + COMBO_X_LOCAL + COMBO_W - ARROW_W;
        const int valueX = m_Pos.x + COMBO_X_LOCAL + COMBO_W / 2 - 12;
        g_pRenderText->RenderText(leftX,  m_Pos.y + ROW_ANISO_CMB_Y, L"<");
        g_pRenderText->RenderText(rightX, m_Pos.y + ROW_ANISO_CMB_Y, L">");
        g_pRenderText->RenderText(valueX, m_Pos.y + ROW_ANISO_CMB_Y, s_AnisoLabels[m_iAnisotropyIdx]);
    }

    // --- VSync row ---
    py = static_cast<float>(m_Pos.y + ROW_VSYNC_Y);
    RenderImage(VQ_IMG_POINT, px, py, 10.f, 10.f);
    g_pRenderText->RenderText(m_Pos.x + TEXT_X_LOCAL, m_Pos.y + ROW_VSYNC_Y + 2, L"VSync");
    if (m_bVSync)
        RenderImage(VQ_IMG_CHECK, cbx, py, 15.f, 15.f, 0, 0);
    else
        RenderImage(VQ_IMG_CHECK, cbx, py, 15.f, 15.f, 0, 15.f);

    // --- MSAA row ---
    py = static_cast<float>(m_Pos.y + ROW_MSAA_LBL_Y);
    RenderImage(VQ_IMG_POINT, px, py, 10.f, 10.f);
    g_pRenderText->RenderText(m_Pos.x + TEXT_X_LOCAL, m_Pos.y + ROW_MSAA_LBL_Y + 2, L"MSAA");
    {
        const int leftX  = m_Pos.x + COMBO_X_LOCAL;
        const int rightX = m_Pos.x + COMBO_X_LOCAL + COMBO_W - ARROW_W;
        const int valueX = m_Pos.x + COMBO_X_LOCAL + COMBO_W / 2 - 12;
        g_pRenderText->RenderText(leftX,  m_Pos.y + ROW_MSAA_CMB_Y, L"<");
        g_pRenderText->RenderText(rightX, m_Pos.y + ROW_MSAA_CMB_Y, L">");
        g_pRenderText->RenderText(valueX, m_Pos.y + ROW_MSAA_CMB_Y, s_MSAALabels[m_iMSAAIdx]);
    }

    // --- Restart note ---
    g_pRenderText->SetTextColor(200, 150, 50, 255); // amber hint
    g_pRenderText->RenderText(m_Pos.x + TEXT_X_LOCAL - 10, m_Pos.y + ROW_NOTE_Y,
                              L"* Mipmap / MSAA require restart");
    g_pRenderText->SetTextColor(255, 255, 255, 255);
}

void CNewUIVisualQualityWindow::RenderButtons()
{
    m_BtnClose.Render();
}

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------

void CNewUIVisualQualityWindow::HandleCheckboxInput()
{
    if (!SEASON3B::IsPress(VK_LBUTTON))
        return;

    // Mipmap checkbox
    if (CheckMouseIn(m_Pos.x + CB_X_LOCAL, m_Pos.y + ROW_MIPMAP_Y, CB_SIZE, CB_SIZE))
    {
        m_bMipmap = !m_bMipmap;
        OnMipmapChanged();
        return;
    }

    // VSync checkbox
    if (CheckMouseIn(m_Pos.x + CB_X_LOCAL, m_Pos.y + ROW_VSYNC_Y, CB_SIZE, CB_SIZE))
    {
        m_bVSync = !m_bVSync;
        OnVSyncChanged();
        return;
    }

    // Anisotropy cycle arrows
    const int aniL = m_Pos.x + COMBO_X_LOCAL;
    const int aniR = m_Pos.x + COMBO_X_LOCAL + COMBO_W - ARROW_W;
    if (CheckMouseIn(aniL, m_Pos.y + ROW_ANISO_CMB_Y, ARROW_W, ARROW_H))
    {
        m_iAnisotropyIdx = (m_iAnisotropyIdx + ANISO_COUNT - 1) % ANISO_COUNT;
        OnAnisotropyChanged();
        return;
    }
    if (CheckMouseIn(aniR, m_Pos.y + ROW_ANISO_CMB_Y, ARROW_W, ARROW_H))
    {
        m_iAnisotropyIdx = (m_iAnisotropyIdx + 1) % ANISO_COUNT;
        OnAnisotropyChanged();
        return;
    }

    // MSAA cycle arrows
    const int msaaL = m_Pos.x + COMBO_X_LOCAL;
    const int msaaR = m_Pos.x + COMBO_X_LOCAL + COMBO_W - ARROW_W;
    if (CheckMouseIn(msaaL, m_Pos.y + ROW_MSAA_CMB_Y, ARROW_W, ARROW_H))
    {
        m_iMSAAIdx = (m_iMSAAIdx + MSAA_COUNT - 1) % MSAA_COUNT;
        OnMSAAChanged();
        return;
    }
    if (CheckMouseIn(msaaR, m_Pos.y + ROW_MSAA_CMB_Y, ARROW_W, ARROW_H))
    {
        m_iMSAAIdx = (m_iMSAAIdx + 1) % MSAA_COUNT;
        OnMSAAChanged();
        return;
    }
}

// ---------------------------------------------------------------------------
// Setting application
// ---------------------------------------------------------------------------

void CNewUIVisualQualityWindow::OnMipmapChanged()
{
    GameConfig& cfg = GameConfig::GetInstance();
    cfg.SetMipmap(m_bMipmap);
    cfg.Save();
    // Mipmap is baked at upload time — requires restart; no live apply.
}

void CNewUIVisualQualityWindow::OnAnisotropyChanged()
{
    GameConfig& cfg = GameConfig::GetInstance();
    cfg.SetAnisotropy(AnisotropyIndexToValue(m_iAnisotropyIdx));
    cfg.Save();
    // Live apply — re-upload texture parameters.
    Bitmaps.ReapplyAllTextureParameters();
}

void CNewUIVisualQualityWindow::OnVSyncChanged()
{
    GameConfig& cfg = GameConfig::GetInstance();
    cfg.SetVSync(m_bVSync);
    cfg.Save();
    if (m_bVSync) EnableVSync(); else DisableVSync();
}

void CNewUIVisualQualityWindow::OnMSAAChanged()
{
    GameConfig& cfg = GameConfig::GetInstance();
    cfg.SetMSAA(MSAAIndexToValue(m_iMSAAIdx));
    cfg.Save();
    // MSAA requires window recreation — requires restart; no live apply.
}

// ---------------------------------------------------------------------------
// Conversion helpers
// ---------------------------------------------------------------------------

int CNewUIVisualQualityWindow::AnisotropyValueToIndex(int value)
{
    for (int i = 0; i < ANISO_COUNT; ++i)
    {
        if (ANISO_VALUES[i] == value)
            return i;
    }
    return 0; // default: Off
}

int CNewUIVisualQualityWindow::AnisotropyIndexToValue(int index)
{
    if (index >= 0 && index < ANISO_COUNT)
        return ANISO_VALUES[index];
    return 0;
}

int CNewUIVisualQualityWindow::MSAAValueToIndex(int value)
{
    for (int i = 0; i < MSAA_COUNT; ++i)
    {
        if (MSAA_VALUES[i] == value)
            return i;
    }
    return 0;
}

int CNewUIVisualQualityWindow::MSAAIndexToValue(int index)
{
    if (index >= 0 && index < MSAA_COUNT)
        return MSAA_VALUES[index];
    return 0;
}
