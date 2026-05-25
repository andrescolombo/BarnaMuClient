// NewUIVisualQualityWindow.h: Visual Quality options panel.
//////////////////////////////////////////////////////////////////////
#pragma once

#include "UI/NewUI/NewUIManager.h"
#include "UI/NewUI/Widgets/NewUIButton.h"
#include "UI/NewUI/Widgets/NewUIComboBox.h"

namespace SEASON3B
{
    class CNewUIVisualQualityWindow : public CNewUIObj
    {
    public:
        CNewUIVisualQualityWindow();
        virtual ~CNewUIVisualQualityWindow();

        bool Create(CNewUIManager* pNewUIMng, int x, int y);
        void Release();

        void SetPos(int x, int y);

        bool UpdateMouseEvent();
        bool UpdateKeyEvent();
        bool Update();
        bool Render();

        float GetLayerDepth();
        float GetKeyEventOrder();

        void OpenningProcess();
        void ClosingProcess();

    private:
        void LoadImages();
        void UnloadImages();
        void SetButtonInfo();

        void RenderFrame();
        void RenderContents();
        void RenderButtons();

        void HandleCheckboxInput();
        void OnMipmapChanged();
        void OnAnisotropyChanged();
        void OnVSyncChanged();
        void OnMSAAChanged();

        static int AnisotropyValueToIndex(int value);
        static int AnisotropyIndexToValue(int index);
        static int MSAAValueToIndex(int value);
        static int MSAAIndexToValue(int index);

    private:
        CNewUIManager* m_pNewUIMng;
        POINT          m_Pos;

        CNewUIButton   m_BtnClose;

        bool m_bMipmap;
        bool m_bVSync;
        int  m_iAnisotropyIdx; // 0=Off,1=2x,2=4x,3=8x,4=16x
        int  m_iMSAAIdx;       // 0=Off,1=2x,2=4x

        CNewUIComboBox m_AnisotropyCombo;
        CNewUIComboBox m_MSAACombo;
    };
}
