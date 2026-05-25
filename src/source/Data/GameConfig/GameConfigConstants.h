#pragma once

namespace CfgSections
{
    inline constexpr wchar_t CfgSectionWindow[]     = L"Window";
    inline constexpr wchar_t CfgSectionGraphics[]   = L"Graphics";
    inline constexpr wchar_t CfgSectionAudio[]      = L"Audio";
    inline constexpr wchar_t CfgSectionUI[]         = L"UI";
    inline constexpr wchar_t CfgSectionLogin[]      = L"LOGIN";
    inline constexpr wchar_t CfgSectionConnectionSettings[] = L"CONNECTION SETTINGS";
    inline constexpr wchar_t CfgSectionCamera[] = L"Camera";
    inline constexpr wchar_t CfgSectionMuHelper[] = L"MuHelper";
}

namespace CfgKeys
{
    // Window
    inline constexpr wchar_t CfgKeyWidth[]      = L"Width";
    inline constexpr wchar_t CfgKeyHeight[]     = L"Height";
    inline constexpr wchar_t CfgKeyWindowed[]   = L"Windowed";

    // Audio — volume 0 = off, >0 = on (no separate Enabled flag).
    inline constexpr wchar_t CfgKeySoundVolume[]  = L"SoundVolume";
    inline constexpr wchar_t CfgKeyMusicVolume[] = L"MusicVolume";

    // Login
    inline constexpr wchar_t CfgKeyRememberMe[]        = L"RememberMe";
    inline constexpr wchar_t CfgKeyLanguage[]          = L"Language";
    inline constexpr wchar_t CfgKeyEncryptedUsername[] = L"EncryptedUsername";
    inline constexpr wchar_t CfgKeyEncryptedPassword[] = L"EncryptedPassword";

    // Connection
    inline constexpr wchar_t CfgKeyServerIP[]            = L"ServerIP";
    inline constexpr wchar_t CfgKeyServerPort[]           = L"ServerPort";
    inline constexpr wchar_t CfgKeyForgotPasswordURL[]    = L"ForgotPasswordURL";

    // Graphics — Visual Quality
    inline constexpr wchar_t CfgKeyAnisotropy[] = L"Anisotropy";
    inline constexpr wchar_t CfgKeyMipmap[]     = L"Mipmap";
    inline constexpr wchar_t CfgKeyVSync[]      = L"VSync";
    inline constexpr wchar_t CfgKeyMSAA[]       = L"MSAA";

    // UI
    inline constexpr wchar_t CfgKeyUILocale[] = L"Locale";

    // Camera
    inline constexpr wchar_t CfgKeyZoom[] = L"Zoom";

    // Mu Helper
    inline constexpr wchar_t CfgKeyFriendRequestMode[] = L"FriendRequestMode";
    inline constexpr wchar_t CfgKeyGuildRequestMode[] = L"GuildRequestMode";
    inline constexpr wchar_t CfgKeyPartyRequestMode[] = L"PartyRequestMode";
}

namespace CfgDefaults
{
    inline constexpr int  CfgDefaultWindowWidth  = 1024;
    inline constexpr int  CfgDefaultWindowHeight = 768;
    inline constexpr bool CfgDefaultWindowed     = true;

    inline constexpr int  CfgDefaultSoundVolume = 5;
    inline constexpr int  CfgDefaultMusicVolume = 5;

    inline constexpr bool CfgDefaultRememberMe = false;
    inline constexpr wchar_t CfgDefaultLanguage[] = L"Eng";
    inline constexpr wchar_t CfgDefaultEncryptedUsername[] = L"";
    inline constexpr wchar_t CfgDefaultEncryptedPassword[] = L"";

    inline constexpr wchar_t CfgDefaultServerIP[] = L"127.127.127.127";
    inline constexpr int     CfgDefaultServerPort = 44406;

    // Graphics — Visual Quality defaults (conservative: match current baseline)
    inline constexpr int  CfgDefaultAnisotropy = 0;     // 0 = off (1.0f)
    inline constexpr bool CfgDefaultMipmap     = false; // off — current renderer baseline is GL_NEAREST
    inline constexpr bool CfgDefaultVSync      = true;
    inline constexpr int  CfgDefaultMSAA       = 0;     // off

    inline constexpr int CfgDefaultZoom = 1735;  // OrbitalCamera DEFAULT_RADIUS — matches Default-cam camera-to-Hero distance
    // I18N locale code; "en" is the default the resx generator falls back to.
    inline constexpr wchar_t CfgDefaultUILocale[] = L"en";
    inline constexpr int CfgDefaultRequestMode = 0;
}
