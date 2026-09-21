// RA2 Yuri's Revenge — LuaAPI Launcher (Win32, dark, custom-painted GDI, no console)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cmath>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {

// ---------------------------------------------------------------------------
// DESIGN TOKENS — colour / size / type. Geometry is DPI-scaled via SS().
// ---------------------------------------------------------------------------
namespace Tok {
constexpr COLORREF WindowBg   = RGB(14, 17, 22);
constexpr COLORREF SidebarBg  = RGB(16, 20, 26);
constexpr COLORREF Surface    = RGB(22, 27, 34);
constexpr COLORREF Surface2   = RGB(30, 37, 47);
constexpr COLORREF SurfaceHov = RGB(46, 55, 68);
constexpr COLORREF CardHover  = RGB(28, 34, 43);

constexpr COLORREF Text      = RGB(230, 237, 243);
constexpr COLORREF TextDim   = RGB(139, 148, 158);
constexpr COLORREF TextFaint = RGB(90, 100, 112);

constexpr COLORREF Accent      = RGB(63, 185, 80);
constexpr COLORREF AccentHover = RGB(91, 204, 111);
constexpr COLORREF Launch      = RGB(218, 54, 51);
constexpr COLORREF LaunchHov   = RGB(240, 84, 82);
constexpr COLORREF Inject      = RGB(31, 111, 235);
constexpr COLORREF InjectHov   = RGB(76, 148, 242);

constexpr COLORREF Warn  = RGB(236, 137, 36);
constexpr COLORREF Ok    = RGB(63, 185, 80);
constexpr COLORREF Error = RGB(218, 54, 51);

constexpr COLORREF Chip        = RGB(48, 54, 64);
constexpr COLORREF Border      = RGB(58, 68, 80);
constexpr COLORREF Divider     = RGB(38, 45, 55);
constexpr COLORREF Disabled    = RGB(52, 60, 72);
constexpr COLORREF ScrollTrack = RGB(36, 43, 53);
constexpr COLORREF ScrollThumb = RGB(88, 97, 108);

constexpr int RadiusCard = 8;
constexpr int RadiusBtn  = 6;
constexpr int RadiusPill = 12;

constexpr int FontTitle = 15;
constexpr int FontH1    = 18;
constexpr int FontH2    = 13;
constexpr int FontBody  = 12;
constexpr int FontCap   = 10;
constexpr int FontStat  = 24;

constexpr int HoverMs = 120;
}

constexpr COLORREF kBg      = Tok::WindowBg;
constexpr COLORREF kSurface = Tok::Surface;
constexpr COLORREF kSurface2= Tok::Surface2;
constexpr COLORREF kHover   = Tok::CardHover;
constexpr COLORREF kRed     = Tok::Launch;
constexpr COLORREF kBlue    = Tok::Inject;
constexpr COLORREF kGreen   = Tok::Accent;
constexpr COLORREF kText    = Tok::Text;
constexpr COLORREF kDim     = Tok::TextDim;
constexpr COLORREF kFaint   = Tok::TextFaint;
constexpr COLORREF kOrange  = Tok::Warn;
constexpr COLORREF kOk      = Tok::Ok;

constexpr const wchar_t* kWindowClass = L"LuaAPIInjectorWnd";
constexpr const wchar_t* kWindowTitle = L"RA2 Yuri's Revenge - LuaAPI";
#define IDI_APP_ICON 101

constexpr int kDefaultClientW = 1100;
constexpr int kDefaultClientH = 740;
constexpr int kMinClientW     = 840;
constexpr int kMinClientH     = 600;

constexpr const wchar_t* kGameProcess = L"gamemd.exe";

// Syringe/Ares/Phobos and CnCNet setups may run the game under a different
// image name. Detection and attach iterate this list in order; the first
// running match wins. kGameProcess stays the default (vanilla launch flow).
constexpr const wchar_t* kGameProcessNames[] = { L"gamemd.exe", L"gamemd-spawn.exe" };
constexpr size_t kGameProcessNameCount =
    sizeof(kGameProcessNames) / sizeof(kGameProcessNames[0]);

// --- DPI --------------------------------------------------------------------
HWND g_hwnd = nullptr;
int WinDpi() {
    static auto pFn = reinterpret_cast<UINT(WINAPI*)(HWND)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (pFn && g_hwnd) {
        UINT d = pFn(g_hwnd);
        if (d) return static_cast<int>(d);
    }
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

int SS(int v) { return MulDiv(v, WinDpi(), 96); }

HFONT CreateFontToken(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, WinDpi(), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

HFONT g_fontTitle = nullptr;
HFONT g_fontH1    = nullptr;
HFONT g_fontH2    = nullptr;
HFONT g_fontBody  = nullptr;
HFONT g_fontCap   = nullptr;
HFONT g_fontStat  = nullptr;

void RecreateFonts() {
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontH1)    DeleteObject(g_fontH1);
    if (g_fontH2)    DeleteObject(g_fontH2);
    if (g_fontBody)  DeleteObject(g_fontBody);
    if (g_fontCap)   DeleteObject(g_fontCap);
    if (g_fontStat)  DeleteObject(g_fontStat);
    g_fontTitle = CreateFontToken(Tok::FontTitle, FW_BOLD);
    g_fontH1    = CreateFontToken(Tok::FontH1,    FW_SEMIBOLD);
    g_fontH2    = CreateFontToken(Tok::FontH2,    FW_SEMIBOLD);
    g_fontBody  = CreateFontToken(Tok::FontBody,  FW_NORMAL);
    g_fontCap   = CreateFontToken(Tok::FontCap,   FW_NORMAL);
    g_fontStat  = CreateFontToken(Tok::FontStat,  FW_BOLD);
}

// --- Localization -----------------------------------------------------------
bool g_isRussian = true;

const wchar_t* L10N(const wchar_t* ru, const wchar_t* en) { return g_isRussian ? ru : en; }

const wchar_t* Str_Subtitle() {
    return L10N(L"Yuri's Revenge v1.001",
                L"Yuri's Revenge v1.001");
}
const wchar_t* Str_NavDashboard() { return L10N(L"\u041F\u0430\u043D\u0435\u043B\u044C", L"Dashboard"); }
const wchar_t* Str_NavMods()      { return L10N(L"\u041C\u043E\u0434\u044B", L"Mods"); }
const wchar_t* Str_NavSettings()  { return L10N(L"\u041D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0438", L"Settings"); }

const wchar_t* St_Ready()       { return L10N(L"\u0413\u043E\u0442\u043E\u0432 \u043A \u0437\u0430\u043F\u0443\u0441\u043A\u0443", L"Ready to Launch"); }
const wchar_t* St_Launching()   { return L10N(L"\u0417\u0430\u043F\u0443\u0441\u043A gamemd.exe\u2026", L"Launching gamemd.exe\u2026"); }
const wchar_t* St_NotInjected() { return L10N(L"\u0418\u0433\u0440\u0430 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430 \u2014 LuaAPI \u043D\u0435 \u0432\u043D\u0435\u0434\u0440\u0435\u043D\u0430", L"Game Running \u2014 LuaAPI Not Injected"); }
const wchar_t* St_Injected()    { return L10N(L"\u0418\u0433\u0440\u0430 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430 \u2014 LuaAPI \u0432\u043D\u0435\u0434\u0440\u0435\u043D\u0430", L"Game Running \u2014 LuaAPI Injected"); }
const wchar_t* St_Injecting()   { return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435\u2026", L"Injecting\u2026"); }
const wchar_t* St_InjectFail()  { return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C", L"Injection Failed"); }
const wchar_t* St_GameNotFound(){ return L10N(L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D\u0430", L"Game Not Found"); }

const wchar_t* Str_LaunchBtn()  { return L10N(L"\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C", L"Launch Game"); }
const wchar_t* Str_InjectBtn()  { return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0438\u0442\u044C", L"Inject"); }
const wchar_t* Str_CncBtn()     { return L"CnCNet"; }
const wchar_t* Str_ReInjectBtn(){ return L10N(L"\u041F\u043E\u0432\u0442\u043E\u0440\u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0438\u0442\u044C", L"Re-inject"); }
const wchar_t* Str_ApplyBtn()   { return L10N(L"\u041F\u0440\u0438\u043C\u0435\u043D\u0438\u0442\u044C", L"Apply Changes"); }
const wchar_t* Str_OpenModsDir(){ return L10N(L"\u041F\u0430\u043F\u043A\u0430 \u043C\u043E\u0434\u043E\u0432", L"Open Mods Folder"); }
const wchar_t* Str_OpenLogs()   { return L10N(L"\u0416\u0443\u0440\u043D\u0430\u043B\u044B", L"Open Logs"); }
const wchar_t* Str_OpenFolder() { return L10N(L"\u041F\u0430\u043F\u043A\u0430 \u043C\u043E\u0434\u0430", L"Open Mod Folder"); }
const wchar_t* Str_OpenLua()    { return L10N(L"\u041E\u0442\u043A\u0440\u044B\u0442\u044C main.lua", L"Open main.lua"); }
const wchar_t* Str_Explorer()   { return L10N(L"\u041F\u043E\u043A\u0430\u0437\u0430\u0442\u044C \u0432 \u043F\u0440\u043E\u0432\u043E\u0434\u043D\u0438\u043A\u0435", L"Show in Explorer"); }
const wchar_t* Str_Enable()     { return L10N(L"\u0412\u043A\u043B\u044E\u0447\u0438\u0442\u044C", L"Enable"); }
const wchar_t* Str_Disable()    { return L10N(L"\u0412\u044B\u043A\u043B\u044E\u0447\u0438\u0442\u044C", L"Disable"); }

std::wstring Str_ActiveCount(int active, int total) {
    return g_isRussian
        ? L"\u0410\u043A\u0442\u0438\u0432\u043D\u043E: " + std::to_wstring(active) + L" \u0438\u0437 " + std::to_wstring(total)
        : L"Active: " + std::to_wstring(active) + L" of " + std::to_wstring(total);
}
std::wstring Str_Problems(int n) {
    return g_isRussian
        ? std::to_wstring(n) + L" \u043F\u0440\u043E\u0431\u043B\u0435\u043C"
        : std::to_wstring(n) + L" problem" + (n == 1 ? L"" : L"s");
}
const wchar_t* Str_NoMods()     { return L10N(L"\u041C\u043E\u0434\u044B \u043D\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043B\u0435\u043D\u044B", L"No mods installed"); }
const wchar_t* Str_NoModsHint() { return L10N(L"\u041F\u043E\u043C\u0435\u0441\u0442\u0438\u0442\u0435 \u043F\u0430\u043F\u043A\u0438 \u0432 scripts/mods/ \u0438 \u043F\u0435\u0440\u0435\u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435", L"Place folders in scripts/mods/ and restart"); }
const wchar_t* Str_NoResults()  { return L10N(L"\u041D\u0438\u0447\u0435\u0433\u043E \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D\u043E", L"No mods match your search"); }
const wchar_t* Str_SearchPh()   { return L10N(L"\u041F\u043E\u0438\u0441\u043A \u043C\u043E\u0434\u043E\u0432\u2026", L"Search mods\u2026"); }
const wchar_t* Str_SelectHint() { return L10N(L"\u0412\u044B\u0431\u0435\u0440\u0438\u0442\u0435 \u043C\u043E\u0434\u2026", L"Select a mod to see its details"); }
const wchar_t* Str_ConflictsTitle(){ return L10N(L"\u041A\u043E\u043D\u0444\u043B\u0438\u043A\u0442\u044B", L"Conflicts"); }
const wchar_t* Str_NoProblems() { return L10N(L"\u041D\u0435\u0442 \u043F\u0440\u043E\u0431\u043B\u0435\u043C", L"No problems detected"); }
const wchar_t* Str_ProblemsTitle(){ return L10N(L"\u041F\u0440\u043E\u0431\u043B\u0435\u043C\u044B", L"Problems"); }
const wchar_t* Str_QuickActions(){ return L10N(L"\u0411\u044B\u0441\u0442\u0440\u044B\u0435 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044F", L"Quick Actions"); }
const wchar_t* Str_StatsMods()  { return L10N(L"\u041C\u043E\u0434\u044B", L"Mods"); }
const wchar_t* Str_StatsActive(){ return L10N(L"\u0410\u043A\u0442\u0438\u0432\u043D\u044B\u0435", L"Enabled"); }
const wchar_t* Str_StatsProblems(){ return L10N(L"\u041F\u0440\u043E\u0431\u043B\u0435\u043C\u044B", L"Problems"); }

const wchar_t* St_SettingsLang(){ return L10N(L"\u042F\u0437\u044B\u043A", L"Language"); }
const wchar_t* St_SettingsGame(){ return L10N(L"\u0418\u0433\u0440\u0430", L"Game"); }
const wchar_t* St_SettingsDiag(){ return L10N(L"\u0414\u0438\u0430\u0433\u043D\u043E\u0441\u0442\u0438\u043A\u0430", L"Diagnostics"); }
const wchar_t* St_SettingsAbout(){ return L10N(L"\u041E \u043F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u0435", L"About"); }
const wchar_t* St_GamePath()    { return L10N(L"\u0420\u0430\u0441\u043F\u043E\u043B\u043E\u0436\u0435\u043D\u0438\u0435 \u0438\u0433\u0440\u044B", L"Game location"); }
const wchar_t* St_Status()      { return L10N(L"\u0421\u0442\u0430\u0442\u0443\u0441", L"Status"); }

std::wstring Str_VersionLine() {
    return L10N(L"\u0412\u0435\u0440\u0441\u0438\u044F 1.3 \u2014 LuaAPI Engine",
                L"Version 1.3 \u2014 LuaAPI Engine");
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class View { Dashboard, Mods, Settings };

struct ModEntry {
    std::wstring dir;
    std::wstring id;
    std::wstring name = L"?";
    std::wstring version = L"1.0.0";
    std::wstring author = L"unknown";
    std::wstring description;
    std::vector<std::wstring> conflicts;
    bool hasManifest = false;
    bool enabled = false;
};

struct GameInfo {
    const wchar_t* headline = St_Ready();
    std::wstring sub;
    COLORREF color = kOk;
    bool canLaunch = false;
    bool canInject = false;
};

struct InjectResult {
    bool ok = false;
    DWORD pid = 0;
    std::wstring error;
};

// FIX: launch workers used to publish the found process name through the
// shared global g_pendingGameName (data race with the UI thread). Results
// are now carried on the heap like InjectResult and owned by the UI thread.
struct LaunchResult {
    bool ok = false;
    DWORD pid = 0;
    std::wstring name;
    std::wstring error;
};

struct DragState {
    bool pendingClick = false;
    bool dragging = false;
    int pendingIndex = -1;
    int dragIndex = -1;
    POINT downPos{0, 0};
    int dragAnchorY = 0;
};

struct Geo {
    RECT sidebar, content, brand;
    RECT navDashboard, navMods, navSettings;
    RECT sidebarStatus;
    RECT hero, launchBtn, injectBtn, cncBtn;
    RECT statMods, statActive, statProbs;
    RECT quickAction1, quickAction2, quickAction3, quickAction4;
    RECT search, list, inspector, applyBtn;
    RECT inspectorBtns[4];
    RECT langSeg;
    RECT diagBtn1, diagBtn2, diagBtn3;
    RECT aboutCard;
    int langY, gameY, gamePathY, gameStatusY, diagY;
    int problemsY;
};
Geo g_geo;

int g_clientW = kDefaultClientW;
int g_clientH = kDefaultClientH;

DWORD g_gamePid = 0;
std::wstring g_gameName;
bool g_injected = false;
bool g_skipInjection = false;
bool g_attachMode = false;
bool g_runCnCNet = false;
std::wstring g_attachTarget;
bool g_launching = false;
bool g_injecting = false;

View g_view = View::Dashboard;
bool g_dirty = false;
bool g_fullscreen = false;
bool g_headless = false;
RECT g_windowedRect{};

constexpr UINT WM_APP_LAUNCH_DONE = WM_APP + 1;
constexpr UINT WM_APP_INJECT_DONE = WM_APP + 2;
constexpr UINT kToastTimerId = 1;
constexpr UINT kHoverTimerId = 2;
constexpr UINT kGamePollTimerId = 3;

std::wstring g_toastText;
bool g_toastActive = false;

// Lightweight tooltip for disabled buttons ("why is this gray?").
std::wstring g_tipText;
bool g_tipShow = false;
POINT g_tipPos{ 0, 0 };
constexpr UINT kTipTimerId = 4;

void HideTooltip(HWND hwnd) {
    g_tipShow = false;
    g_tipText.clear();
    if (hwnd) KillTimer(hwnd, kTipTimerId);
}

enum class StatusKey { Ready, GameNotFound, DllMissing, InjectFail, Custom };
StatusKey g_statusKey = StatusKey::Ready;
std::wstring g_statusCustom;
COLORREF g_statusColor = kOk;

std::vector<ModEntry> g_mods;

RECT g_rcSearch{};
bool g_searchFocused = false;
bool g_hoverSearch = false;
std::wstring g_searchQuery;
std::vector<int> g_visible;
int g_scroll = 0;
int g_selected = -1;

bool g_trackingMouse = false;
bool g_down = false;
View g_hoverNav = static_cast<View>(-1);
int g_hoverRow = -1;
bool g_hoverLaunch = false, g_hoverInject = false, g_hoverApply = false, g_hoverCnc = false;
bool g_hoverQA1 = false, g_hoverQA2 = false, g_hoverQA3 = false, g_hoverQA4 = false;
bool g_hoverD1 = false, g_hoverD2 = false, g_hoverD3 = false;
int g_hoverBtn = 0;  // inspector button index

DragState g_dragState;

constexpr int kSidebarW = 224;
constexpr int kRowH = 64;
constexpr int kRowGap = 6;
constexpr int kScrollW = 6;
constexpr int kInspectorW = 340;

int SidebarW() { return SS(kSidebarW); }
inline int RowStep() { return SS(kRowH) + SS(kRowGap); }

void RecalcLayout();
void ClampScroll();
void RebuildVisible();

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------
void FillRoundRect(HDC dc, const RECT& r, COLORREF fill, int radius, COLORREF outline = 0, bool hasOutline = false) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = hasOutline ? CreatePen(PS_SOLID, 1, outline) : reinterpret_cast<HPEN>(GetStockObject(NULL_PEN));
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    if (hasOutline) DeleteObject(pen);
}

void DrawCircle(HDC dc, int cx, int cy, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = reinterpret_cast<HPEN>(GetStockObject(NULL_PEN));
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
}

void DrawTextR(HDC dc, const std::wstring& text, RECT rc, HFONT font, COLORREF color,
               UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rc, flags | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old);
}

int TextWidth(HDC dc, const std::wstring& text, HFONT font) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SIZE sz{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

void DrawCheckbox(HDC dc, const RECT& box, bool enabled) {
    if (enabled) {
        FillRoundRect(dc, box, Tok::Accent, SS(5));
        HPEN pen = CreatePen(PS_SOLID, SS(2), Tok::Text);
        auto oldPen = SelectObject(dc, pen);
        int s = (box.right - box.left) / 7;
        MoveToEx(dc, box.left + s,     box.top + s * 10 / 7, nullptr);
        LineTo(dc,   box.left + s * 3, box.top + s * 14 / 7);
        LineTo(dc,   box.left + s * 6, box.top + s * 4 / 7);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    } else {
        FillRoundRect(dc, box, Tok::Surface, SS(5), Tok::Border, true);
    }
}

void DrawFolderIcon(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left + SS(2), r.top + SS(6), r.right - SS(2), r.bottom - SS(2), SS(3), SS(3));
    Rectangle(dc, r.left + SS(2), r.top + SS(3), r.left + SS(10), r.top + SS(8));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawPencilIcon(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    POINT body[4] = {
        {r.left + SS(3),  r.bottom - SS(4)},
        {r.left + SS(7),  r.bottom - SS(8)},
        {r.right - SS(8), r.top + SS(6)},
        {r.right - SS(4), r.top + SS(2)}
    };
    Polygon(dc, body, 4);
    POINT tip[3] = {
        {r.right - SS(2), r.top},
        {r.right - SS(10), r.top + SS(1)},
        {r.right - SS(5), r.top + SS(6)}
    };
    Polygon(dc, tip, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

std::wstring ToUpper(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}

std::wstring Str_AppVersion() { return L"v1.3"; }

// Muted professional avatar palette (white glyph on top).
COLORREF AvatarColor(const std::wstring& id) {
    static const COLORREF pal[6] = {
        RGB(31, 111, 135), RGB(146, 106, 28), RGB(108, 66, 146),
        RGB(46, 92, 160), RGB(150, 62, 84), RGB(56, 128, 74),
    };
    unsigned h = 0;
    for (wchar_t c : id) h = h * 31u + static_cast<unsigned>(c);
    return pal[h % 6];
}

void DrawAvatar(HDC dc, const RECT& r, const std::wstring& name, COLORREF col) {
    FillRoundRect(dc, r, col, SS(8));
    wchar_t ch[2] = { name.empty() ? L'?' : static_cast<wchar_t>(towupper(name[0])), 0 };
    DrawTextR(dc, ch, r, g_fontH2, RGB(240, 244, 248), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Small vector glyphs for action buttons (drawn, no icon font needed).
enum class Glyph { Play, Inject, Cnc, Power };

void DrawGlyph(HDC dc, const RECT& r, Glyph g, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, SS(2), color);
    HBRUSH brush = CreateSolidBrush(color);
    auto oldPen = SelectObject(dc, pen);
    auto oldBrush = SelectObject(dc, brush);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int s = std::min(r.right - r.left, r.bottom - r.top) / 2;
    if (s < SS(4)) s = SS(4);
    if (g == Glyph::Play) {
        POINT p[3] = { {cx - s/2, cy - s}, {cx - s/2, cy + s}, {cx + s, cy} };
        Polygon(dc, p, 3);
    } else if (g == Glyph::Inject) {
        MoveToEx(dc, cx, cy - s, nullptr); LineTo(dc, cx, cy + s / 2);
        POINT p[3] = { {cx - s/2, cy}, {cx + s/2, cy}, {cx, cy + s/2 + SS(2)} };
        Polygon(dc, p, 3);
        MoveToEx(dc, cx - s, cy + s, nullptr); LineTo(dc, cx + s, cy + s);
    } else if (g == Glyph::Cnc) {
        POINT p[4] = { {cx, cy - s}, {cx + s, cy}, {cx, cy + s}, {cx - s, cy} };
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Polygon(dc, p, 4);
    } else {  // Power
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Arc(dc, cx - s, cy - s, cx + s, cy + s, cx + s/3, cy - s, cx - s/3, cy - s);
        MoveToEx(dc, cx, cy - s, nullptr); LineTo(dc, cx, cy + s/3);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawCircleOutline(HDC dc, int cx, int cy, int radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, SS(2), color);
    auto oldPen = SelectObject(dc, pen);
    auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}

// Outlined pill chip, e.g. version / status. Returns nothing; text centered.
void DrawChip(HDC dc, const RECT& r, const std::wstring& text, COLORREF fg, COLORREF bg) {
    FillRoundRect(dc, r, bg, SS(10), fg, true);
    DrawTextR(dc, text, RECT{ r.left + SS(8), r.top, r.right - SS(8), r.bottom },
              g_fontCap, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DriveSeg(HDC dc, const RECT& act) {
    HPEN pen = CreatePen(PS_SOLID, SS(2), Tok::Accent);
    auto oldPen = SelectObject(dc, pen);
    int ax = (act.left + act.right) / 2 - SS(10);
    MoveToEx(dc, ax, act.bottom - SS(6), nullptr);
    LineTo(dc, ax + SS(20), act.bottom - SS(6));
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(GetRValue(a) + static_cast<int>((GetRValue(b) - GetRValue(a)) * t),
               GetGValue(a) + static_cast<int>((GetGValue(b) - GetGValue(a)) * t),
               GetBValue(a) + static_cast<int>((GetBValue(b) - GetBValue(a)) * t));
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetExeDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true) {
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) return L".";
        if (len < path.size() - 1 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
        path.resize(path.size() * 2);
    }
    path.resize(len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring GetPrefsPath() { return GetExeDirectory() + L"\\injector.ini"; }
void LoadPrefs() {
    wchar_t buf[16] = {0};
    GetPrivateProfileStringW(L"UI", L"lang", L"RU", buf, 16, GetPrefsPath().c_str());
    g_isRussian = (_wcsicmp(buf, L"EN") != 0);
}
void SavePrefs() {
    WritePrivateProfileStringW(L"UI", L"lang", g_isRussian ? L"RU" : L"EN", GetPrefsPath().c_str());
}
void LogLine(const std::wstring& text) {
    // FIX: launch/inject workers call LogLine off the UI thread; serialize
    // file appends so lines never interleave. SRWLOCK needs no explicit
    // init (zero-initialized statics are valid), so first use from any
    // thread is race-free.
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    std::wofstream log(GetExeDirectory() + L"\\injector_log.txt", std::ios::app);
    SYSTEMTIME st; GetLocalTime(&st);
    log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"." << st.wMilliseconds << L"] " << text << L"\n";
    ReleaseSRWLockExclusive(&lock);
}

void ShowToast(const std::wstring& msg) {
    g_toastText = msg;
    g_toastActive = true;
    if (g_hwnd) { KillTimer(g_hwnd, kToastTimerId); SetTimer(g_hwnd, kToastTimerId, 2200, nullptr); }
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void SetStatusKey(StatusKey key) {
    g_statusKey = key;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}
void SetStatusCustom(const std::wstring& text) {
    g_statusKey = StatusKey::Custom;
    g_statusCustom = text;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// Game-state model
// ---------------------------------------------------------------------------
bool PointIn(const RECT& r, POINT p);  // defined in the Layout section below
GameInfo ComputeGameInfo() {
    GameInfo gi;
    std::wstring exeDir = GetExeDirectory();
    bool dllExists = FileExists(exeDir + L"\\LuaAPI.dll");
    bool gameRunning = (g_gamePid != 0);

    if (g_launching) { gi.headline = St_Launching(); gi.color = kOrange; }
    else if (g_injecting) { gi.headline = St_Injecting(); gi.color = kOrange; }
    else if (gameRunning && g_injected) {
        gi.headline = St_Injected(); gi.color = kOk; gi.sub = L"PID " + std::to_wstring(g_gamePid);
    } else if (gameRunning) {
        gi.headline = St_NotInjected(); gi.color = kBlue;
        gi.canInject = dllExists && !g_injecting;
        gi.sub = L"PID " + std::to_wstring(g_gamePid);
    } else if (g_statusKey == StatusKey::InjectFail) {
        gi.headline = St_InjectFail(); gi.color = kRed;
    } else {
        gi.headline = dllExists ? St_Ready() : St_GameNotFound();
        gi.color = dllExists ? kOk : kRed;
        gi.canLaunch = dllExists && !g_launching;
    }
    return gi;
}

// Explains a DISABLED button under the cursor (empty = none / enabled).
std::wstring DisabledReason(POINT pt) {
    if (g_view == View::Dashboard) {
        GameInfo gi = ComputeGameInfo();
        bool dllExists = FileExists(GetExeDirectory() + L"\\LuaAPI.dll");
        if (PointIn(g_geo.launchBtn, pt) && !gi.canLaunch) {
            if (!dllExists) return L10N(L"Нет файла LuaAPI.dll", L"LuaAPI.dll is missing");
            if (g_launching) return L10N(L"Запуск…", L"Launching…");
            return L10N(L"Игра уже запущена — используйте «Внедрить»", L"Game is already running — use Inject");
        }
        if (PointIn(g_geo.injectBtn, pt) && !gi.canInject) {
            if (!dllExists) return L10N(L"Нет файла LuaAPI.dll", L"LuaAPI.dll is missing");
            if (g_injecting) return L10N(L"Внедрение…", L"Injecting…");
            if (g_gamePid != 0 && g_injected) return L10N(L"LuaAPI уже внедрена", L"LuaAPI is already injected");
            return L10N(L"Сначала запустите игру", L"Launch the game first");
        }
        if (PointIn(g_geo.quickAction3, pt) && (g_gamePid == 0 || g_injecting))
            return L10N(L"Сначала запустите игру", L"Launch the game first");
    } else if (g_view == View::Mods) {
        if (PointIn(g_geo.applyBtn, pt) && !g_dirty)
            return L10N(L"Нет несохранённых изменений", L"No unsaved changes");
    } else {
        if (PointIn(g_geo.diagBtn3, pt) && (g_gamePid == 0 || g_injecting))
            return L10N(L"Сначала запустите игру", L"Launch the game first");
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Process helpers
// ---------------------------------------------------------------------------
DWORD FindTargetProcess(std::wstring* outName = nullptr) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    std::wstring found;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            for (size_t i = 0; i < kGameProcessNameCount && pid == 0; ++i) {
                if (_wcsicmp(entry.szExeFile, kGameProcessNames[i]) != 0) continue;
                HANDLE moduleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, entry.th32ProcessID);
                if (moduleSnap != INVALID_HANDLE_VALUE) {
                    MODULEENTRY32W mod{}; mod.dwSize = sizeof(mod);
                    if (Module32FirstW(moduleSnap, &mod) && _wcsicmp(mod.szModule, kGameProcessNames[i]) == 0) {
                        pid = entry.th32ProcessID;
                        found = kGameProcessNames[i];
                    }
                    CloseHandle(moduleSnap);
                }
            }
            if (pid) break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (pid && outName) *outName = found;
    return pid;
}

bool InjectDllIntoProcess(DWORD pid, const std::wstring& dllPath, std::wstring* error) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        DWORD ec = GetLastError();
        if (error) {
            *error = L"OpenProcess failed (error " + std::to_wstring(ec) + L")";
            if (ec == ERROR_ACCESS_DENIED)
                *error += L" \u2014 access denied, try running the launcher as administrator";
        }
        return false;
    }
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteBase = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBase) { if (error) *error = L"VirtualAllocEx failed (error " + std::to_wstring(GetLastError()) + L")"; CloseHandle(process); return false; }
    if (!WriteProcessMemory(process, remoteBase, dllPath.c_str(), bytes, nullptr)) {
        if (error) *error = L"WriteProcessMemory failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
    }
    auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLibraryW) {
        if (error) *error = L"GetProcAddress(LoadLibraryW) failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
    }
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remoteBase, 0, nullptr);
    if (!thread) {
        if (error) *error = L"CreateRemoteThread failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
    }
    DWORD waitResult = WaitForSingleObject(thread, 5000);
    if (waitResult == WAIT_TIMEOUT) {
        if (error) *error = L"Injection timed out: target did not load DLL within 5000 ms (WAIT_TIMEOUT)";
        // FIX: never VirtualFreeEx here — the remote thread may still be
        // reading the path buffer; freeing it could crash the game.
        // The leaked region is one path string; handles are still closed.
        CloseHandle(thread); CloseHandle(process); return false;
    }
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
    CloseHandle(process);
    if (exitCode == 0) { if (error) *error = L"LoadLibraryW returned NULL inside the target"; return false; }
    return true;
}

DWORD FindProcessByName(const wchar_t* exeName) {
    if (!exeName || !*exeName) return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    DWORD pid = 0;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    if (Process32FirstW(snap, &e)) {
        do { if (_wcsicmp(e.szExeFile, exeName) == 0) { pid = e.th32ProcessID; break; } } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName) {
    uintptr_t base = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m{}; m.dwSize = sizeof(m);
    if (Module32FirstW(snap, &m)) {
        do { if (_wcsicmp(m.szModule, moduleName) == 0) { base = reinterpret_cast<uintptr_t>(m.modBaseAddr); break; } } while (Module32NextW(snap, &m));
    }
    CloseHandle(snap);
    return base;
}

std::vector<std::wstring> GetProcessModules(DWORD pid) {
    std::vector<std::wstring> mods;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return mods;
    MODULEENTRY32W m{}; m.dwSize = sizeof(m);
    if (Module32FirstW(snap, &m)) { do { mods.emplace_back(m.szModule); } while (Module32NextW(snap, &m)); }
    CloseHandle(snap);
    return mods;
}

bool ReadLiveBytes(DWORD pid, uintptr_t addr, uint8_t* buf, size_t n) {
    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) return false;
    SIZE_T read = 0;
    bool ok = ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addr), buf, n, &read) && read == n;
    CloseHandle(proc);
    return ok;
}

std::wstring BytesToHexStr(const uint8_t* bytes, size_t n) {
    wchar_t b[8]; std::wstring out;
    for (size_t i = 0; i < n; ++i) {
        swprintf(b, 8, L"%02X", bytes[i]); out += b;
        if (i + 1 < n) out += L' ';
    }
    return out;
}
std::wstring HexWord(uintptr_t v) { wchar_t b[16]; swprintf(b, 16, L"0x%08X", static_cast<unsigned int>(v)); return b; }

// ---------------------------------------------------------------------------
// Game process discovery + liveness (polled on a timer; g_gamePid is the cache)
// ---------------------------------------------------------------------------
bool IsPidAlive(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        // An invalid PID means the process is gone. Other failures (e.g. an
        // elevated game that the launcher can't open) should not be treated as death.
        return GetLastError() != ERROR_INVALID_PARAMETER;
    }
    bool alive = (WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    if (!alive) { DWORD code = 0; if (GetExitCodeProcess(h, &code)) alive = (code == STILL_ACTIVE); }
    CloseHandle(h);
    return alive;
}

void RefreshGameProcessState() {
    bool changed = false;
    if (g_gamePid != 0) {
        if (IsPidAlive(g_gamePid)) return;
        LogLine(L"Game process PID " + std::to_wstring(g_gamePid) + L" no longer running; clearing state");
        g_gamePid = 0;
        g_gameName.clear();
        g_injected = false;
        g_injecting = false;
        changed = true;
    } else {
        std::wstring foundName;
        DWORD pid = FindTargetProcess(&foundName);
        if (pid) {
            LogLine(L"Detected running " + foundName + L" (PID " + std::to_wstring(pid) + L")");
            g_gamePid = pid;
            g_gameName = foundName;
            changed = true;
        }
    }
    if (changed && g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// Launch / inject
// ---------------------------------------------------------------------------
void DoLaunchGameAsync(HWND hwnd);
void DoInjectAttachAsync(HWND hwnd, DWORD pid, const std::wstring& dllPath);

void DoInjectAttach() {
    if (g_injecting) return;
    LogLine(L"Inject: searching for a running game process (gamemd.exe / gamemd-spawn.exe)...");
    g_statusKey = StatusKey::Ready;  // drop a stale InjectFail/GameNotFound banner
    std::wstring foundName;
    DWORD pid = FindTargetProcess(&foundName);
    if (pid == 0) {
        LogLine(L"Inject: process not found");
        SetStatusKey(StatusKey::GameNotFound);
        MessageBoxW(g_hwnd,
                    L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430 (gamemd.exe / gamemd-spawn.exe).\n\n"
                    L"\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0438\u0433\u0440\u0443 \u2014 \u0432\u0430\u043D\u0438\u043B\u044C\u043D\u0443\u044E \u043A\u043D\u043E\u043F\u043A\u043E\u0439 \u00AB\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C \u0438\u0433\u0440\u0443\u00BB \u0438\u043B\u0438 \u0447\u0435\u0440\u0435\u0437 Syringe (Ares/Phobos) \u2014 \u0437\u0430\u0442\u0435\u043C \u043D\u0430\u0436\u043C\u0438\u0442\u0435 \u00AB\u0412\u043D\u0435\u0434\u0440\u0438\u0442\u044C\u00BB.",
                    L"\u041F\u043E\u0438\u0441\u043A \u043F\u0440\u043E\u0446\u0435\u0441\u0441\u0430", MB_ICONWARNING | MB_OK);
        return;
    }
    LogLine(L"Inject: found " + foundName + L" (PID " + std::to_wstring(pid) + L")");
    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        LogLine(L"Inject: LuaAPI.dll missing at " + dllPath);
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    LogLine(L"Inject: dispatching attachment thread (PID " + std::to_wstring(pid) + L", DLL " + dllPath + L")");
    g_gamePid = pid;
    g_gameName = foundName;
    g_injecting = true;
    ShowToast(St_Injecting());
    HWND hwnd = g_hwnd;
    std::thread([hwnd, pid, dllPath]() { DoInjectAttachAsync(hwnd, pid, dllPath); }).detach();
}

void DoInjectAttachAsync(HWND hwnd, DWORD pid, const std::wstring& dllPath) {
    LogLine(L"Inject: attempting injection into PID " + std::to_wstring(pid));
    std::wstring error;
    bool ok = InjectDllIntoProcess(pid, dllPath, &error);
    LogLine(ok ? L"Inject: injection call OK (PID " + std::to_wstring(pid) + L")"
               : L"Inject: injection call FAILED (PID " + std::to_wstring(pid) + L"): " + error);
    PostMessageW(hwnd, WM_APP_INJECT_DONE, 0, reinterpret_cast<LPARAM>(new InjectResult{ ok, pid, error }));
}

void DoLaunchGame() {
    if (g_launching) return;
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    DWORD existing = FindTargetProcess(&g_gameName);
    if (existing != 0) {
        // FIX: the old code injected synchronously on the UI thread
        // (visible freeze) with an unused extra process handle. Reuse the
        // async attach path instead.
        LogLine(L"Launch: game already running (PID " + std::to_wstring(existing) + L"), attaching instead");
        DoInjectAttach();
        return;
    }
    g_launching = true;
    g_statusKey = StatusKey::Ready;
    ShowToast(St_Launching());
    InvalidateRect(g_hwnd, nullptr, TRUE);
    HWND hwnd = g_hwnd;
    std::thread([hwnd]() { DoLaunchGameAsync(hwnd); }).detach();
}

void DoLaunchGameAsync(HWND hwnd) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    std::wstring stubPath = exeDir + L"\\RA2MD.EXE";
    if (FileExists(stubPath)) {
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(stubPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    bool injected = false;
    std::wstring err;
    DWORD foundPid = 0;
    std::wstring foundName;
    for (int i = 0; i < 600; ++i) {
        Sleep(200);
        std::wstring nm;
        DWORD pid = FindTargetProcess(&nm);
        if (!pid) continue;
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process) continue;
        bool ok = InjectDllIntoProcess(pid, dllPath, &err);
        CloseHandle(process);
        foundPid = pid; foundName = nm;
        if (ok) injected = true;
        break;
    }
    auto* res = new LaunchResult{ injected, foundPid, foundName, err };
    PostMessageW(hwnd, WM_APP_LAUNCH_DONE, 0, reinterpret_cast<LPARAM>(res));
}

// ---------------------------------------------------------------------------
// CnCNet launch: start the CnCNet client (it spawns the game itself as
// gamemd-spawn.exe), then auto-inject LuaAPI into the game process —
// "starts already injected". If the game is already running (vanilla,
// Syringe, or a previous CnCNet session), just inject into it.
//
// Research notes (CnCNet YR package = XNA client + SyringeEx + Ares +
// Phobos + yrpp-spawner, game spawned straight into battle via spawn.ini):
// the spawned process loads Syringe-side DLLs at birth, so injection waits
// for the hook-host modules first (same bounded wait as headless --attach);
// MinHook chaining with them is covered by M11 Gate 11.2.
// ---------------------------------------------------------------------------
void DoLaunchCnCNetAsync(HWND hwnd, const std::wstring& clientPath);
void WaitForHookHostModules(DWORD pid);

// Entry point order: the official CnCNet YR launcher first (it
// self-updates and brings up the XNA client, which is absent until the
// first launcher run), then XNA clients (root or Resources/, where the
// YR package stages clientdx/clientxna/clientogl), then legacy names.
std::wstring FindCnCNetClient(const std::wstring& exeDir) {
    static const wchar_t* kCnCNetClients[] = {
        L"CnCNetYRLauncher.exe",
        L"Resources\\clientdx.exe", L"Resources\\clientxna.exe", L"Resources\\clientogl.exe",
        L"CnCNetClient.exe", L"clientdx.exe", L"clientxna.exe", L"CnCNet.exe"
    };
    for (const wchar_t* c : kCnCNetClients) {
        std::wstring cand = exeDir + L"\\" + c;
        if (FileExists(cand)) return cand;
    }
    return L"";
}

// Synchronous building blocks shared by the headless entry points.
// (The GUI keeps its async wrappers; headless must block, otherwise the
// process would exit and kill the detached worker mid-launch.)
bool StartStubProcess(const std::wstring& exeDir, const std::wstring& stubFile) {
    std::wstring p = exeDir + L"\\" + stubFile;
    if (!FileExists(p)) { LogLine(L"Headless: stub missing: " + p); return false; }
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(p.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, exeDir.c_str(), &si, &pi)) {
        LogLine(L"Headless: CreateProcess failed for " + p + L" (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

bool WaitForGameProcess(DWORD* outPid, std::wstring* outName, DWORD timeoutMs = 120000) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < timeoutMs) {
        std::wstring nm;
        DWORD pid = FindTargetProcess(&nm);
        if (pid) { if (outPid) *outPid = pid; if (outName) *outName = nm; return true; }
        Sleep(200);
    }
    return false;
}

void WaitForGameExit(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0; GetExitCodeProcess(h, &code);
    wchar_t b[16]; swprintf(b, 16, L"%08X", code);
    LogLine(L"Headless: game PID " + std::to_wstring(pid) + L" exited code=0x" + std::wstring(b));
    CloseHandle(h);
}

// FIX: --noinject (g_skipInjection) was parsed but never read — the flag
// launched *with* injection exactly like a normal launch. Now honoured.
int RunHeadlessLaunch() {
    LogLine(L"--- Headless launch started ---");
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!g_skipInjection && !FileExists(dllPath)) {
        LogLine(L"Headless: LuaAPI.dll missing at " + dllPath);
        return 1;
    }
    if (!StartStubProcess(exeDir, L"RA2MD.EXE")) return 1;
    DWORD pid = 0; std::wstring name;
    if (!WaitForGameProcess(&pid, &name)) {
        LogLine(L"Headless: no game process appeared within 120 s");
        return 1;
    }
    LogLine(L"Headless: detected " + name + L" (PID " + std::to_wstring(pid) + L")");
    if (!g_skipInjection) {
        std::wstring err;
        if (!InjectDllIntoProcess(pid, dllPath, &err)) {
            LogLine(L"Headless: injection FAILED: " + err);
            return 1;
        }
        LogLine(L"Headless: LuaAPI.dll injected into PID " + std::to_wstring(pid));
    } else {
        LogLine(L"Headless: --noinject, skipping injection");
    }
    WaitForGameExit(pid);
    return 0;
}

int RunHeadlessCnCNet() {
    LogLine(L"--- Headless CnCNet launch started ---");
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) { LogLine(L"Headless: LuaAPI.dll missing at " + dllPath); return 1; }
    std::wstring clientPath = FindCnCNetClient(exeDir);
    if (clientPath.empty()) { LogLine(L"Headless: no CnCNet client found"); return 1; }
    std::wstring clientDir = clientPath;
    size_t slash = clientDir.find_last_of(L"\\/");
    clientDir = (slash == std::wstring::npos) ? exeDir : clientDir.substr(0, slash);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(clientPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, clientDir.c_str(), &si, &pi)) {
        LogLine(L"Headless: CnCNet CreateProcess failed (error " + std::to_wstring(GetLastError()) + L")");
        return 1;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    DWORD pid = 0; std::wstring name;
    if (!WaitForGameProcess(&pid, &name)) {
        LogLine(L"Headless: no game process appeared within 120 s");
        return 1;
    }
    WaitForHookHostModules(pid);
    std::wstring err;
    if (!InjectDllIntoProcess(pid, dllPath, &err)) {
        LogLine(L"Headless: injection FAILED: " + err);
        return 1;
    }
    LogLine(L"Headless: LuaAPI.dll injected into " + name + L" (PID " + std::to_wstring(pid) + L")");
    WaitForGameExit(pid);
    return 0;
}

// Bounded wait for Syringe/Ares/Phobos/spawner modules inside the target.
// Never blocks forever: on timeout the caller proceeds anyway (same policy
// as headless RunAttachWait).
void WaitForHookHostModules(DWORD pid) {
    static const wchar_t* kWaitDlls[] = { L"Ares.dll", L"Phobos.dll", L"CnCNet-Spawner.dll" };
    ULONGLONG modStart = GetTickCount64();
    while (GetTickCount64() - modStart < 15000) {
        auto mods = GetProcessModules(pid);
        bool allPresent = true;
        for (const wchar_t* dll : kWaitDlls) {
            bool found = false;
            for (const auto& m : mods) { if (_wcsicmp(m.c_str(), dll) == 0) { found = true; break; } }
            if (!found) { allPresent = false; break; }
        }
        if (allPresent) break;
        Sleep(500);
    }
    Sleep(1000);
}

void DoLaunchCnCNet() {
    if (g_launching || g_injecting) return;
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    // Game already up (any known image)? Inject right away, no relaunch.
    std::wstring running;
    if (FindTargetProcess(&running) != 0) { DoInjectAttach(); return; }

    std::wstring clientPath = FindCnCNetClient(exeDir);
    if (clientPath.empty()) {
        MessageBoxW(g_hwnd,
                    L"CnCNet YR \u043F\u0430\u043A\u0435\u0442 \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D \u0432 \u043F\u0430\u043F\u043A\u0435 \u0438\u0433\u0440\u044B.\n\n"
                    L"\u0421\u043A\u0430\u0447\u0430\u0439\u0442\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u0449\u0438\u043A CnCNet Yuri's Revenge \u0441 cncnet.org "
                    L"\u0438 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u0438\u0442\u0435 \u0435\u0433\u043E \u0432 \u044D\u0442\u0443 \u043F\u0430\u043F\u043A\u0443. "
                    L"\u041F\u0430\u043A\u0435\u0442 \u043F\u0440\u0438\u043D\u043E\u0441\u0438\u0442 \u043A\u043B\u0438\u0435\u043D\u0442 (CnCNetClient.exe / clientdx.exe), "
                    L"Syringe, Ares, Phobos \u0438 \u0441\u043F\u0430\u0432\u043D\u0435\u0440 (gamemd-spawn.exe) — "
                    L"\u043A\u043D\u043E\u043F\u043A\u0430 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442 \u043A\u043B\u0438\u0435\u043D\u0442 \u0438 \u0430\u0432\u0442\u043E\u0432\u043D\u0435\u0434\u0440\u0438\u0442 LuaAPI "
                    L"\u0432 \u0437\u0430\u0441\u043F\u0430\u0432\u043D\u0435\u043D\u043D\u0443\u044E \u0438\u0433\u0440\u0443.",
                    L"CnCNet", MB_ICONWARNING | MB_OK);
        return;
    }
    g_launching = true;
    g_statusKey = StatusKey::Ready;
    ShowToast(St_Launching());
    InvalidateRect(g_hwnd, nullptr, TRUE);
    HWND hwnd = g_hwnd;
    std::thread([hwnd, clientPath]() { DoLaunchCnCNetAsync(hwnd, clientPath); }).detach();
}

void DoLaunchCnCNetAsync(HWND hwnd, const std::wstring& clientPath) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    LogLine(L"CnCNet: starting client " + clientPath);
    // The client resolves theme/config paths relative to its own directory
    // (e.g. Resources\clientdx.exe), so run it with CWD = its folder.
    std::wstring clientDir = clientPath;
    size_t slash = clientDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) clientDir = clientDir.substr(0, slash);
    else clientDir = exeDir;
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(clientPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, clientDir.c_str(), &si, &pi)) {
        LogLine(L"CnCNet: CreateProcess failed (error " + std::to_wstring(GetLastError()) + L")");
        PostMessageW(hwnd, WM_APP_LAUNCH_DONE, 0, 0);
        return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    // Wait for the spawned game (gamemd-spawn.exe normally) and inject.
    // The spawner loads Syringe-side DLLs first — wait for them so MinHook
    // never races their prologue patching.
    std::wstring err;
    std::wstring foundName;
    DWORD foundPid = 0;
    bool injected = false;
    for (int i = 0; i < 600; ++i) {
        Sleep(200);
        std::wstring nm;
        DWORD pid = FindTargetProcess(&nm);
        if (!pid) continue;
        WaitForHookHostModules(pid);
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process) continue;
        bool ok = InjectDllIntoProcess(pid, dllPath, &err);
        CloseHandle(process);
        foundPid = pid; foundName = nm;
        if (ok) injected = true;
        break;
    }
    if (foundPid) {
        LogLine(L"CnCNet: game " + foundName + L" (PID " + std::to_wstring(foundPid) + L") injection " +
                (injected ? L"OK" : L"FAILED: " + err));
    } else {
        LogLine(L"CnCNet: no game process appeared within 120 s");
    }
    auto* res = new LaunchResult{ injected, foundPid, foundName, err };
    PostMessageW(hwnd, WM_APP_LAUNCH_DONE, 0, reinterpret_cast<LPARAM>(res));
}

// ---------------------------------------------------------------------------
// Attach mode (headless)
// ---------------------------------------------------------------------------
int RunAttachWait(const std::wstring& explicitName) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        MessageBoxW(nullptr, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return 1;
    }
    std::vector<std::wstring> targets;
    if (!explicitName.empty()) targets.push_back(explicitName);
    else { targets.push_back(L"gamemd-spawn.exe"); targets.push_back(L"gamemd.exe"); }

    constexpr DWORD kWaitMs = 120000; constexpr DWORD kPollMs = 500;
    DWORD pid = 0; std::wstring foundName; ULONGLONG startTick = GetTickCount64();
    while (GetTickCount64() - startTick < kWaitMs) {
        for (const auto& t : targets) { DWORD p = FindProcessByName(t.c_str()); if (p != 0) { pid = p; foundName = t; break; } }
        if (pid) break;
        Sleep(kPollMs);
    }
    if (pid == 0) return 1;

    static const wchar_t* kWaitDlls[] = { L"Ares.dll", L"Phobos.dll", L"CnCNet-Spawner.dll" };
    bool allPresent = false; ULONGLONG modStart = GetTickCount64();
    while (GetTickCount64() - modStart < 15000) {
        auto mods = GetProcessModules(pid);
        allPresent = true;
        for (const wchar_t* dll : kWaitDlls) {
            bool found = false;
            for (const auto& m : mods) { if (_wcsicmp(m.c_str(), dll) == 0) { found = true; break; } }
            if (!found) { allPresent = false; break; }
        }
        if (allPresent) break;
        Sleep(500);
    }
    Sleep(1000);
    std::wstring error;
    if (!InjectDllIntoProcess(pid, dllPath, &error)) { LogLine(L"Attach: injection FAILED: " + error); return 1; }
    g_gamePid = pid; g_gameName = foundName; g_injected = true;
    LogLine(L"Attach: LuaAPI.dll injected into PID " + std::to_wstring(pid));
    HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        WaitForSingleObject(hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(hProcess, &code);
        wchar_t b[16]; swprintf(b, 16, L"%08X", code);
        LogLine(L"Attach: exited code=0x" + std::wstring(b));
        CloseHandle(hProcess);
    }
    Sleep(500);
    return 0;
}

// ---------------------------------------------------------------------------
// Mods — data
// ---------------------------------------------------------------------------
std::vector<std::wstring> LoadActiveModIds(const std::wstring& exeDir) {
    std::vector<std::wstring> ids;
    std::ifstream file(exeDir + L"\\scripts\\active_mods.txt");
    std::string line; bool first = true;
    while (std::getline(file, line)) {
        if (first) { first = false; if (line.size() >= 3 && (unsigned char)line[0]==0xEF && (unsigned char)line[1]==0xBB && (unsigned char)line[2]==0xBF) line.erase(0,3); }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;
        std::string id = line.substr(start);
        size_t end = id.find_last_not_of(" \t");
        if (end != std::string::npos) id = id.substr(0, end+1);
        ids.push_back(std::wstring(id.begin(), id.end()));
    }
    return ids;
}

std::wstring JsonGetString(const std::wstring& json, const wchar_t* key) {
    std::wstring pattern = std::wstring(L"\"") + key + L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos) return L"";
    size_t colon = json.find(L':', keyPos + pattern.size());
    size_t openQuote = json.find(L'"', colon);
    size_t closeQuote = json.find(L'"', openQuote + 1);
    if (colon == std::wstring::npos || openQuote == std::wstring::npos || closeQuote == std::wstring::npos) return L"";
    return json.substr(openQuote + 1, closeQuote - openQuote - 1);
}

std::vector<std::wstring> JsonGetStringArray(const std::wstring& json, const wchar_t* key) {
    std::vector<std::wstring> out;
    std::wstring pattern = std::wstring(L"\"") + key + L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos) return out;
    size_t openBracket = json.find(L'[', keyPos);
    size_t closeBracket = json.find(L']', openBracket == std::wstring::npos ? 0 : openBracket);
    if (openBracket == std::wstring::npos || closeBracket == std::wstring::npos || closeBracket <= openBracket) return out;
    std::wstring body = json.substr(openBracket + 1, closeBracket - openBracket - 1);
    size_t pos = 0;
    while ((pos = body.find(L'"', pos)) != std::wstring::npos) {
        size_t end = body.find(L'"', pos + 1);
        if (end == std::wstring::npos) break;
        out.push_back(body.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return out;
}

void ScanMods() {
    g_mods.clear();
    g_scroll = 0;
    std::wstring exeDir = GetExeDirectory();
    auto activeIds = LoadActiveModIds(exeDir);
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((exeDir + L"\\scripts\\mods\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) { RebuildVisible(); return; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        ModEntry entry{};
        entry.dir = fd.cFileName;
        entry.id = fd.cFileName;
        std::wstring manifest = exeDir + L"\\scripts\\mods\\" + entry.dir + L"\\mod.json";
        if (FileExists(manifest)) {
            std::ifstream f(manifest);
            std::stringstream ss; ss << f.rdbuf();
            int size = MultiByteToWideChar(CP_UTF8, 0, ss.str().c_str(), -1, nullptr, 0);
            if (size > 0) {
                std::wstring wide(static_cast<size_t>(size), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, ss.str().c_str(), -1, &wide[0], size);
                wide.resize(size - 1);
                entry.id = JsonGetString(wide, L"id");
                entry.name = JsonGetString(wide, L"name");
                entry.version = JsonGetString(wide, L"version");
                entry.author = JsonGetString(wide, L"author");
                entry.description = JsonGetString(wide, L"description");
                entry.conflicts = JsonGetStringArray(wide, L"conflicts");
                entry.hasManifest = true;
                if (entry.id.empty()) entry.id = entry.dir;
                if (entry.name.empty()) entry.name = entry.id;
            }
        } else if (!FileExists(exeDir + L"\\scripts\\mods\\" + entry.dir + L"\\main.lua")) {
            continue;
        }
        if (entry.name.empty()) entry.name = entry.id;
        if (entry.author.empty()) entry.author = L"unknown";
        for (const auto& id : activeIds)
            if (_wcsicmp(entry.id.c_str(), id.c_str()) == 0) { entry.enabled = true; break; }
        g_mods.push_back(entry);
    } while (FindNextFileW(find, &fd));
    FindClose(find);

    if (!activeIds.empty()) {
        std::vector<ModEntry> ordered; std::vector<bool> used(g_mods.size(), false);
        for (const auto& id : activeIds)
            for (size_t i = 0; i < g_mods.size(); ++i)
                if (!used[i] && _wcsicmp(g_mods[i].id.c_str(), id.c_str()) == 0) { ordered.push_back(std::move(g_mods[i])); used[i] = true; break; }
        for (size_t i = 0; i < g_mods.size(); ++i) if (!used[i]) ordered.push_back(std::move(g_mods[i]));
        g_mods = std::move(ordered);
    }
    RebuildVisible();
}

void SaveMods() {
    std::wstring exeDir = GetExeDirectory();
    std::wofstream out(exeDir + L"\\scripts\\active_mods.txt", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        MessageBoxW(g_hwnd, L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0437\u0430\u043F\u0438\u0441\u0430\u0442\u044C active_mods.txt",
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    out << L"# LuaAPI active mods - one mod ID per line\n";
    for (const auto& m : g_mods) if (m.enabled) out << m.id << L"\n";
    out.flush(); out.close();
    g_dirty = false;
    ShowToast(L10N(L"\u2713 \u0421\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u043E", L"\u2713 Saved"));
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

int EnabledModCount() { int n = 0; for (const auto& m : g_mods) if (m.enabled) ++n; return n; }

bool ModHealthValid(const ModEntry& m) {
    return FileExists(GetExeDirectory() + L"\\scripts\\mods\\" + m.dir + L"\\main.lua");
}

bool MatchesFilter(const ModEntry& m, const std::wstring& q) {
    if (q.empty()) return true;
    auto lower = [](std::wstring s) { for (auto& c : s) c = towlower(c); return s; };
    std::wstring needle = lower(q);
    return lower(m.name).find(needle) != std::wstring::npos ||
           lower(m.author).find(needle) != std::wstring::npos ||
           lower(m.description).find(needle) != std::wstring::npos ||
           lower(m.id).find(needle) != std::wstring::npos;
}

void RebuildVisible() {
    g_visible.clear();
    for (size_t i = 0; i < g_mods.size(); ++i)
        if (MatchesFilter(g_mods[i], g_searchQuery))
            g_visible.push_back(static_cast<int>(i));
    g_scroll = 0;
    if (!g_visible.empty()) g_selected = std::min(g_selected, static_cast<int>(g_visible.size()) - 1);
    else g_selected = -1;
    ClampScroll();
}

std::vector<std::pair<int, int>> DetectConflicts() {
    std::vector<std::pair<int, int>> hits;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!g_mods[i].enabled) continue;
        for (size_t j = i + 1; j < g_mods.size(); ++j) {
            if (!g_mods[j].enabled) continue;
            bool conflict = false;
            for (const auto& c : g_mods[i].conflicts) if (_wcsicmp(c.c_str(), g_mods[j].id.c_str())==0) { conflict = true; break; }
            if (!conflict) for (const auto& c : g_mods[j].conflicts) if (_wcsicmp(c.c_str(), g_mods[i].id.c_str())==0) { conflict = true; break; }
            if (conflict) hits.emplace_back(static_cast<int>(i), static_cast<int>(j));
        }
    }
    return hits;
}

int ProblemsForIndex(int modIdx, const std::vector<std::pair<int, int>>& conflicts) {
    int n = 0;
    for (auto& c : conflicts)
        if (c.first == modIdx || c.second == modIdx) ++n;
    if (modIdx >= 0 && modIdx < static_cast<int>(g_mods.size()) && !ModHealthValid(g_mods[modIdx])) ++n;
    return n;
}

int TotalProblems() {
    int n = 0;
    for (const auto& m : g_mods) if (!ModHealthValid(m)) ++n;
    // FIX: conflict pairs were silently dropped (`return n + 0`), so the
    // Dashboard "Problems" tile showed 0 while conflicts were listed below.
    return n + static_cast<int>(DetectConflicts().size());
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void ClampScroll() {
    RECT l = g_geo.list;
    int listHeight = l.bottom - l.top;
    int totalModHeight = static_cast<int>(g_visible.size()) * RowStep();
    int maxScroll = std::max(0, totalModHeight - listHeight);
    g_scroll = std::max(0, std::min(g_scroll, maxScroll));
}

void RecalcLayout() {
    int w = g_clientW, h = g_clientH;
    int sw = SidebarW();
    g_geo.sidebar = RECT{ 0, 0, sw, h };
    g_geo.content = RECT{ sw, 0, w, h };

    int pad = SS(24);
    int cx = sw + pad;
    int cw = (w - sw) - pad * 2;

    // Sidebar
    g_geo.brand = RECT{ SS(20), SS(16), sw - SS(16), SS(84) };
    int navX = SS(12), navW = sw - SS(24), itemH = SS(40);
    int navY = SS(96);
    g_geo.navDashboard = RECT{ navX, navY, navX + navW, navY + itemH };
    g_geo.navMods      = RECT{ navX, navY + (itemH + SS(4)), navX + navW, navY + (itemH + SS(4)) + itemH };
    g_geo.navSettings  = RECT{ navX, navY + (itemH + SS(4)) * 2, navX + navW, navY + (itemH + SS(4)) * 2 + itemH };
    int footH = SS(64);
    g_geo.sidebarStatus = RECT{ SS(12), h - footH, sw - SS(12), h - SS(12) };

    if (g_view == View::Dashboard) {
        int topY = SS(56);   // below the "Dashboard" title
        // FIX: the old single-row hero (text left + 3 fixed 210px buttons
        // right) overlapped by ~150px even at default width. Headline now
        // spans the full width on top; the three actions share one
        // responsive row below it, so nothing overlaps at any window size.
        int heroH = SS(184);
        g_geo.hero = RECT{ cx, topY, cx + cw, topY + heroH };
        int gap = SS(8);
        int statW = (cw - gap * 2) / 3;
        int statY = g_geo.hero.bottom + SS(16);
        int statH = SS(88);
        g_geo.statMods  = RECT{ cx, statY, cx + statW, statY + statH };
        g_geo.statActive= RECT{ cx + statW + gap, statY, cx + statW*2 + gap, statY + statH };
        g_geo.statProbs = RECT{ cx + statW*2 + gap*2, statY, cx + statW*3 + gap*2, statY + statH };
        int qy = statY + statH + SS(44);
        int qbw = (cw - gap * 3) / 4;
        int qbh = SS(38);
        g_geo.quickAction1 = RECT{ cx, qy, cx + qbw, qy + qbh };
        g_geo.quickAction2 = RECT{ cx + qbw + gap, qy, cx + qbw*2 + gap, qy + qbh };
        g_geo.quickAction3 = RECT{ cx + qbw*2 + gap*2, qy, cx + qbw*3 + gap*2, qy + qbh };
        g_geo.quickAction4 = RECT{ cx + qbw*3 + gap*3, qy, cx + qbw*4 + gap*3, qy + qbh };
        g_geo.problemsY = qy + qbh + SS(40);
        // Hero action row (bottom of hero, three equal buttons).
        int pad2 = SS(24);
        int btnH2 = SS(40);
        int btnGap = SS(12);
        int btnW2 = (cw - pad2 * 2 - btnGap * 2) / 3;
        int by2 = g_geo.hero.bottom - pad2 - btnH2;
        g_geo.cncBtn    = RECT{ cx + pad2, by2, cx + pad2 + btnW2, by2 + btnH2 };
        g_geo.launchBtn = RECT{ cx + pad2 + btnW2 + btnGap, by2, cx + pad2 + btnW2 * 2 + btnGap, by2 + btnH2 };
        g_geo.injectBtn = RECT{ cx + pad2 + btnW2 * 2 + btnGap * 2, by2, cx + pad2 + btnW2 * 3 + btnGap * 2, by2 + btnH2 };
    } else if (g_view == View::Mods) {
        int topY = pad;
        int searchW = SS(280);
        g_geo.search = RECT{ cx + cw - searchW, topY, cx + cw, topY + SS(34) };
        int listTop = topY + SS(56);
        int bottomY = h - pad - SS(52);
        int inspectorW = SS(kInspectorW);
        int midGap = SS(12);
        int listRight = cx + cw - inspectorW - midGap;
        g_geo.list = RECT{ cx, listTop, listRight, bottomY };
        g_geo.inspector = RECT{ listRight + midGap, listTop, cx + cw, bottomY };
        int barTop = h - pad - SS(40);
        g_geo.applyBtn = RECT{ cx + cw - SS(200), barTop, cx + cw, h - pad };
        // inspector action buttons (3, at bottom of inspector)
        int ipad = SS(16);
        int ibtnH = SS(36);
        int ibtnGap = SS(8);
        int ix = g_geo.inspector.left + ipad;
        int iw = (g_geo.inspector.right - g_geo.inspector.left) - ipad * 2;
        int iy = g_geo.inspector.bottom - ipad - ibtnH;
        g_geo.inspectorBtns[0] = RECT{ ix, iy, ix + iw, iy + ibtnH };
        int iy2 = iy - ibtnH - ibtnGap;
        g_geo.inspectorBtns[1] = RECT{ ix, iy2, ix + iw, iy2 + ibtnH };
        int iy3 = iy2 - ibtnH - ibtnGap;
        g_geo.inspectorBtns[2] = RECT{ ix, iy3, ix + iw, iy3 + ibtnH };
        for (int k = 3; k < 4; ++k) g_geo.inspectorBtns[k] = RECT{};
    } else {
        int topY = SS(56);   // below the "Settings" title
        int labelH = SS(26);
        int sectGap = SS(30);
        int sy = topY;
        g_geo.langY = sy;
        g_geo.langSeg = RECT{ cx, sy + labelH + SS(10), cx + SS(200), sy + labelH + SS(10) + SS(34) };
        sy = g_geo.langSeg.bottom + sectGap;
        g_geo.gameY = sy;
        g_geo.gamePathY = sy + labelH + SS(8);
        g_geo.gameStatusY = g_geo.gamePathY + SS(30) + SS(10);
        sy = g_geo.gameStatusY + labelH + sectGap;
        g_geo.diagY = sy;
        int dy = sy + labelH + SS(12);
        int bh = SS(38);
        g_geo.diagBtn1 = RECT{ cx, dy, cx + SS(170), dy + bh };
        g_geo.diagBtn2 = RECT{ cx + SS(182), dy, cx + SS(352), dy + bh };
        g_geo.diagBtn3 = RECT{ cx + SS(364), dy, cx + SS(534), dy + bh };
        // About card anchored to lower content area
        int cardY = h - pad - SS(96);
        g_geo.aboutCard = RECT{ cx, cardY, cx + cw, cardY + SS(84) };
    }

    ClampScroll();
}

bool PointIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

int RowIndexAt(POINT pt) {
    if (g_view != View::Mods) return -1;
    RECT l = g_geo.list;
    if (pt.y < l.top || pt.y > l.bottom || pt.x < l.left || pt.x > l.right) return -1;
    int rowStep = RowStep();
    int rowH = SS(kRowH);
    int yPos = l.top + SS(4) - g_scroll;
    for (size_t i = 0; i < g_visible.size(); ++i) {
        RECT rc = { l.left, yPos, l.right - SS(kScrollW + 4), yPos + rowH };
        if (pt.y >= rc.top && pt.y <= rc.bottom) return static_cast<int>(i);
        yPos += rowStep;
    }
    return -1;
}

int RowCheckAt(POINT pt, int* outIdx) {
    int idx = RowIndexAt(pt);
    if (idx < 0) return 0;
    RECT l = g_geo.list;
    int rowH = SS(kRowH);
    int yPos = l.top + SS(4) - g_scroll + idx * RowStep();
    int sz = SS(18);
    int cy = yPos + rowH / 2;
    RECT box{ l.left + SS(16), cy - sz/2, l.left + SS(16) + sz, cy + sz/2 };
    if (pt.x >= box.left && pt.x <= box.right) { if (outIdx) *outIdx = idx; return 1; }
    return 0;
}

int RowQuickAt(POINT pt, int* outIdx) {
    int idx = RowIndexAt(pt);
    if (idx < 0) return 0;
    RECT l = g_geo.list;
    int rowH = SS(kRowH);
    int yPos = l.top + SS(4) - g_scroll + idx * RowStep();
    int iconW = SS(26);
    int cy = yPos + rowH / 2;
    RECT pencil{ l.right - SS(kScrollW + 4) - SS(10) - iconW, cy - iconW/2, l.right - SS(kScrollW + 4) - SS(10), cy + iconW/2 };
    RECT folder{ pencil.left - SS(6) - iconW, pencil.top, pencil.left - SS(6), pencil.bottom };
    if (pt.x >= folder.left && pt.x <= folder.right && pt.y >= folder.top && pt.y <= folder.bottom) { if (outIdx) *outIdx = idx; return 1; }
    if (pt.x >= pencil.left && pt.x <= pencil.right && pt.y >= pencil.top && pt.y <= pencil.bottom) { if (outIdx) *outIdx = idx; return 2; }
    return 0;
}

std::wstring ModDirFor(int visIdx) {
    if (visIdx < 0 || visIdx >= static_cast<int>(g_visible.size())) return L"";
    int gi = g_visible[visIdx];
    if (gi < 0 || gi >= static_cast<int>(g_mods.size())) return L"";
    return GetExeDirectory() + L"\\scripts\\mods\\" + g_mods[gi].dir;
}
const ModEntry* ModFor(int visIdx) {
    if (visIdx < 0 || visIdx >= static_cast<int>(g_visible.size())) return nullptr;
    int gi = g_visible[visIdx];
    if (gi < 0 || gi >= static_cast<int>(g_mods.size())) return nullptr;
    return &g_mods[gi];
}

// Visible (possibly filtered) row -> index into g_mods, or -1.
int GlobalIndexFor(int visIdx) {
    if (visIdx < 0 || visIdx >= static_cast<int>(g_visible.size())) return -1;
    int gi = g_visible[visIdx];
    if (gi < 0 || gi >= static_cast<int>(g_mods.size())) return -1;
    return gi;
}

void EnsureVisible(int visIdx) {
    if (visIdx < 0 || visIdx >= static_cast<int>(g_visible.size())) return;
    int rowTop = g_geo.list.top + SS(4) + visIdx * RowStep() - g_scroll;
    int rowBot = rowTop + SS(kRowH);
    if (rowTop < g_geo.list.top) g_scroll -= (g_geo.list.top - rowTop);
    else if (rowBot > g_geo.list.bottom) g_scroll += (rowBot - g_geo.list.bottom);
    ClampScroll();
}

void OpenPath(const std::wstring& path, const wchar_t* verb = L"open") {
    ShellExecuteW(g_hwnd, verb, path.c_str(), nullptr, nullptr, SW_SHOW);
}
void OpenModsDir() { OpenPath(GetExeDirectory() + L"\\scripts\\mods", L"explore"); }
void OpenLogs() {
    std::wstring p = GetExeDirectory() + L"\\injector_log.txt";
    if (FileExists(p)) OpenPath(p); else OpenPath(GetExeDirectory(), L"explore");
}
void OpenModFolder(int visIdx) { std::wstring d = ModDirFor(visIdx); if (!d.empty()) OpenPath(d, L"explore"); }
void OpenModLua(int visIdx) {
    std::wstring d = ModDirFor(visIdx);
    if (!d.empty()) { if (FileExists(d + L"\\main.lua")) OpenPath(d + L"\\main.lua"); else OpenPath(d, L"explore"); }
}
void ToggleModEnable(int visIdx) {
    const ModEntry* m = ModFor(visIdx);
    if (!m) return;
    g_mods[g_visible[visIdx]].enabled = !g_mods[g_visible[visIdx]].enabled;
    g_dirty = true;
    InvalidateRect(g_hwnd, nullptr, TRUE);
}
void SelectMod(int visIdx) { g_selected = visIdx; InvalidateRect(g_hwnd, nullptr, TRUE); }
void SetView(View v) {
    if (g_view == v) return;
    g_view = v;
    if (v == View::Mods && g_selected < 0 && !g_visible.empty()) g_selected = 0;
    RecalcLayout();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
bool DrawButton(HDC dc, const RECT& r, const std::wstring& text, COLORREF base, COLORREF hover,
                bool hovered, bool pressed, bool enabled, HFONT font) {
    COLORREF fill;
    if (!enabled) fill = Tok::Disabled;
    else if (pressed) fill = LerpColor(base, kBg, 0.22f);
    else if (hovered) fill = hover;
    else fill = base;
    FillRoundRect(dc, r, fill, Tok::RadiusBtn);
    RECT cr{ r.left + SS(4), r.top, r.right - SS(4), r.bottom };
    DrawTextR(dc, text, cr, font, enabled ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return true;
}

void DrawActionButton(HDC dc, const RECT& r, const std::wstring& text, Glyph glyph,
                      COLORREF base, COLORREF hover, bool hovered, bool pressed,
                      bool enabled, HFONT font) {
    COLORREF fill;
    if (!enabled) fill = Tok::Disabled;
    else if (pressed) fill = LerpColor(base, kBg, 0.22f);
    else if (hovered) fill = hover;
    else fill = base;
    FillRoundRect(dc, r, fill, Tok::RadiusBtn);
    COLORREF fg = enabled ? kText : kDim;
    int isz = SS(16);
    int icy = (r.top + r.bottom) / 2;
    RECT gr{ r.left + SS(18), icy - isz / 2, r.left + SS(18) + isz, icy + isz / 2 };
    DrawGlyph(dc, gr, glyph, fg);
    RECT cr{ gr.right + SS(10), r.top, r.right - SS(10), r.bottom };
    DrawTextR(dc, text, cr, font, fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void PaintContentTitle(HDC dc, const wchar_t* title, const std::wstring& rightHint) {
    int pad = SS(24);
    RECT t{ g_geo.content.left + pad, SS(12), g_geo.content.right - pad, SS(48) };
    if (title && *title)
        DrawTextR(dc, title, t, g_fontH1, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (!rightHint.empty())
        DrawTextR(dc, rightHint, t, g_fontCap, kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void PaintStatTile(HDC dc, const RECT& r, const std::wstring& value, const std::wstring& label, COLORREF accent) {
    FillRoundRect(dc, r, Tok::Surface, Tok::RadiusCard, Tok::Border, true);
    RECT vr{ r.left + SS(16), r.top + SS(4), r.right - SS(16), r.top + SS(56) };
    DrawTextR(dc, value, vr, g_fontStat, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT lr{ r.left + SS(16), r.top + SS(58), r.right - SS(16), r.bottom - SS(6) };
    DrawTextR(dc, ToUpper(label), lr, g_fontCap, accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void PaintSidebar(HDC dc) {
    RECT sr = g_geo.sidebar;
    HBRUSH bg = CreateSolidBrush(Tok::SidebarBg);
    FillRect(dc, &sr, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, Tok::Divider);
    auto oldPen = SelectObject(dc, pen);
    MoveToEx(dc, sr.right - 1, 0, nullptr); LineTo(dc, sr.right - 1, sr.bottom);
    SelectObject(dc, oldPen); DeleteObject(pen);

    RECT br = g_geo.brand;
    int mono = SS(40);
    RECT logo{ br.left, br.top, br.left + mono, br.top + mono };
    FillRoundRect(dc, logo, kGreen, SS(9));
    DrawTextR(dc, L"\u03BB", logo, g_fontH1, RGB(10, 14, 10),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int tx = logo.right + SS(12);
    DrawTextR(dc, L"LUA ENGINE", RECT{ tx, br.top, br.right, br.top + SS(26) },
              g_fontTitle, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, Str_Subtitle(), RECT{ tx, br.top + SS(28), br.right, br.top + SS(46) },
              g_fontCap, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT chip{ tx, br.top + SS(48), tx + TextWidth(dc, Str_AppVersion(), g_fontCap) + SS(16), br.top + SS(64) };
    DrawChip(dc, chip, Str_AppVersion(), kGreen, Tok::Surface);

    struct NavIt { RECT rc; View view; const wchar_t* label; };
    NavIt items[3] = { { g_geo.navDashboard, View::Dashboard, Str_NavDashboard() },
                       { g_geo.navMods, View::Mods, Str_NavMods() },
                       { g_geo.navSettings, View::Settings, Str_NavSettings() } };
    for (auto& it : items) {
        bool activeSel = (g_view == it.view);
        bool hovered = (g_hoverNav == it.view);
        if (activeSel) FillRoundRect(dc, it.rc, Tok::Surface2, SS(6));
        else if (hovered) FillRoundRect(dc, it.rc, Tok::Surface, SS(6));
        RECT icon{ it.rc.left + SS(16), it.rc.top + SS(12), it.rc.left + SS(34), it.rc.top + SS(30) };
        FillRoundRect(dc, icon, activeSel ? kGreen : kFaint, SS(4));
        DrawTextR(dc, it.label, RECT{ it.rc.left + SS(46), it.rc.top, it.rc.right, it.rc.bottom },
                  g_fontBody, (activeSel || hovered) ? kText : kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    RECT fs = g_geo.sidebarStatus;
    GameInfo gi = ComputeGameInfo();
    FillRoundRect(dc, fs, Tok::Surface, SS(8), Tok::Border, true);
    int dotCy = (fs.top + fs.bottom) / 2;
    DrawCircle(dc, fs.left + SS(18), dotCy, SS(5), gi.color);
    DrawTextR(dc, gi.headline, RECT{ fs.left + SS(32), fs.top + SS(8), fs.right - SS(10), fs.top + SS(26) },
              g_fontBody, gi.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, gi.sub, RECT{ fs.left + SS(32), fs.top + SS(28), fs.right - SS(10), fs.bottom - SS(6) },
              g_fontCap, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void PaintDashboard(HDC dc) {
    PaintContentTitle(dc, Str_NavDashboard(), L"");
    GameInfo gi = ComputeGameInfo();
    RECT h = g_geo.hero;
    FillRoundRect(dc, h, Tok::Surface, Tok::RadiusCard, Tok::Border, true);

    // Headline block spans the full hero width; buttons live in their own
    // row (rects from RecalcLayout) so text and buttons never overlap.
    DrawTextR(dc, gi.headline, RECT{ h.left + SS(24), h.top + SS(24), h.right - SS(24), h.top + SS(66) },
              g_fontH1, gi.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (!gi.sub.empty())
        DrawTextR(dc, gi.sub, RECT{ h.left + SS(24), h.top + SS(70), h.right - SS(24), h.top + SS(96) },
                  g_fontBody, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawActionButton(dc, g_geo.cncBtn, Str_CncBtn(), Glyph::Cnc, Tok::Inject, Tok::InjectHov,
               g_hoverCnc, g_down && g_hoverCnc, (!g_launching && !g_injecting), g_fontBody);
    DrawActionButton(dc, g_geo.launchBtn, Str_LaunchBtn(), Glyph::Play, Tok::Launch, Tok::LaunchHov,
               g_hoverLaunch, g_down && g_hoverLaunch, gi.canLaunch, g_fontBody);
    DrawActionButton(dc, g_geo.injectBtn, Str_InjectBtn(), Glyph::Inject, Tok::Inject, Tok::InjectHov,
               g_hoverInject, g_down && g_hoverInject, gi.canInject, g_fontBody);

    int pad = SS(24);
    int enabled = EnabledModCount();
    PaintStatTile(dc, g_geo.statMods, std::to_wstring(g_mods.size()), Str_StatsMods(), kDim);
    PaintStatTile(dc, g_geo.statActive, std::to_wstring(enabled), Str_StatsActive(), kGreen);
    PaintStatTile(dc, g_geo.statProbs, std::to_wstring(TotalProblems()), Str_StatsProblems(), kOrange);

    // Quick actions
    int qy = g_geo.quickAction1.top;
    DrawTextR(dc, Str_QuickActions(), RECT{ g_geo.content.left + pad, qy - SS(30), g_geo.content.right - pad, qy - SS(6) },
              g_fontH2, kFaint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawButton(dc, g_geo.quickAction1, Str_OpenModsDir(), Tok::Surface2, Tok::SurfaceHov,
               g_hoverQA1, g_down && g_hoverQA1, true, g_fontBody);
    DrawButton(dc, g_geo.quickAction2, Str_OpenLogs(), Tok::Surface2, Tok::SurfaceHov,
               g_hoverQA2, g_down && g_hoverQA2, true, g_fontBody);
    DrawButton(dc, g_geo.quickAction3, Str_ReInjectBtn(), Tok::Surface2, Tok::SurfaceHov,
               g_hoverQA3, g_down && g_hoverQA3, (!g_injecting && g_gamePid != 0), g_fontBody);
    DrawButton(dc, g_geo.quickAction4, Str_NavSettings(), Tok::Surface2, Tok::SurfaceHov,
               g_hoverQA4, g_down && g_hoverQA4, true, g_fontBody);

    // Problems
    int py = g_geo.problemsY;
    DrawTextR(dc, Str_ProblemsTitle(), RECT{ g_geo.content.left + pad, py, g_geo.content.right - pad, py + SS(26) },
              g_fontH2, kFaint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    auto conflicts = DetectConflicts();
    bool anyProblem = !conflicts.empty();
    for (auto& m : g_mods) if (!ModHealthValid(m)) { anyProblem = true; break; }

    int py2 = py + SS(30);
    RECT pr{ g_geo.content.left + pad, py2, g_geo.content.right - pad, py2 + SS(120) };
    if (anyProblem) {
        int lineMax = 2, li = 0, total = static_cast<int>(conflicts.size());
        for (auto& m : g_mods) if (!ModHealthValid(m)) ++total;
        for (auto& c : conflicts) {
            if (li >= lineMax) break;
            DrawTextR(dc, L"\u26A0 " + g_mods[c.first].name + L"  vs  " + g_mods[c.second].name,
                      RECT{ pr.left, pr.top + li * SS(26), pr.right, pr.top + (li + 1) * SS(26) },
                      g_fontBody, kOrange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            ++li;
        }
        for (auto& m : g_mods) {
            if (li >= lineMax) break;
            if (!ModHealthValid(m)) {
                DrawTextR(dc, L"\u26A0 " + m.name + L" \u2014 main.lua " + L10N(L"\u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D", L"missing"),
                          RECT{ pr.left, pr.top + li * SS(26), pr.right, pr.top + (li + 1) * SS(26) },
                          g_fontBody, kOrange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                ++li;
            }
        }
        if (total > li) {
            std::wstring more = g_isRussian ? L"+ \u0435\u0449\u0451 " + std::to_wstring(total - li)
                                            : L"+ " + std::to_wstring(total - li) + L" more";
            DrawTextR(dc, more, RECT{ pr.left, pr.top + li * SS(26), pr.right, pr.top + (li + 1) * SS(26) },
                      g_fontCap, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        DrawTextR(dc, std::wstring(L"\u2713 ") + Str_NoProblems(), pr, g_fontBody, kGreen, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

void PaintRow(HDC dc, const RECT& row, const ModEntry& m, bool selected, bool hovered, int problems) {
    if (selected) FillRoundRect(dc, row, Tok::Surface2, SS(6), Tok::Accent, true);
    else if (hovered) FillRoundRect(dc, row, kHover, SS(6), Tok::Border, true);
    else FillRoundRect(dc, row, Tok::Surface, SS(6), Tok::Border, true);

    int cy = (row.top + row.bottom) / 2;
    int sz = SS(18);
    RECT box{ row.left + SS(16), cy - sz/2, row.left + SS(16) + sz, cy + sz/2 };
    DrawCheckbox(dc, box, m.enabled);

    int av = SS(32);
    RECT avr{ box.right + SS(12), cy - av / 2, box.right + SS(12) + av, cy + av / 2 };
    DrawAvatar(dc, avr, m.name, AvatarColor(m.id));

    int nx = avr.right + SS(12);
    int iconW = SS(26) * 2 + SS(6);
    int right = row.right - SS(kScrollW + 4) - SS(10);
    std::wstring st = m.enabled ? (g_isRussian ? L"ВКЛ" : L"ON")
                                : (g_isRussian ? L"ВЫКЛ" : L"OFF");
    int pillW = TextWidth(dc, st, g_fontCap) + SS(18);
    int verW = TextWidth(dc, L"v" + m.version, g_fontCap);
    int metaX = right - iconW - SS(10) - pillW - SS(8) - verW;
    RECT verR{ metaX, cy - SS(10), metaX + verW, cy + SS(10) };
    DrawTextR(dc, L"v" + m.version, verR, g_fontCap, kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT pill{ verR.right + SS(8), cy - SS(11), verR.right + SS(8) + pillW, cy + SS(11) };
    DrawChip(dc, pill, st, m.enabled ? kGreen : kDim, Tok::Surface2);
    int nameEnd = verR.left - SS(10);

    DrawTextR(dc, m.name, RECT{ nx, row.top + SS(8), nameEnd, cy + SS(4) },
              g_fontBody, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    std::wstring desc = m.description.empty() ? m.id : m.description;
    DrawTextR(dc, desc, RECT{ nx, cy + SS(4), nameEnd, row.bottom - SS(6) },
              g_fontCap, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int pxl = row.right - SS(kScrollW + 4) - SS(10) - SS(26);
    RECT pencil{ pxl, cy - SS(13), pxl + SS(26), cy + SS(13) };
    RECT folder{ pencil.left - SS(6) - SS(26), pencil.top, pencil.left - SS(6), pencil.bottom };
    FillRoundRect(dc, folder, Tok::Surface2, Tok::RadiusBtn, Tok::Border, true);
    DrawFolderIcon(dc, folder, kDim);
    FillRoundRect(dc, pencil, Tok::Surface2, Tok::RadiusBtn, Tok::Border, true);
    DrawPencilIcon(dc, pencil, kDim);
    // Conflict badge: amber dot on the avatar corner.
    if (problems > 0) {
        DrawCircle(dc, avr.right - SS(2), avr.top + SS(2), SS(5), kOrange);
    }
}

void PaintInspector(HDC dc, int modIdx) {
    const ModEntry& sel = g_mods[modIdx];
    RECT ins = g_geo.inspector;
    int pad = SS(16);
    int ix = ins.left + pad;
    int iw = (ins.right - ins.left) - pad * 2;
    int yy = ins.top + pad;

    int av = SS(40);
    RECT avr{ ix, yy, ix + av, yy + av };
    DrawAvatar(dc, avr, sel.name, AvatarColor(sel.id));
    DrawTextR(dc, sel.name, RECT{ avr.right + SS(12), yy, ix + iw, yy + SS(26) },
              g_fontH1, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, L"v" + sel.version + L"  \u00B7  by " + sel.author,
              RECT{ avr.right + SS(12), yy + SS(26), ix + iw, yy + SS(48) },
              g_fontCap, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    yy += av + SS(12);

    int probs = ProblemsForIndex(modIdx, DetectConflicts());
    std::wstring stTxt = sel.enabled ? (g_isRussian ? L"ВКЛЮЧЁН" : L"ENABLED")
                                     : (g_isRussian ? L"ВЫКЛЮЧЕН" : L"DISABLED");
    int chipH = SS(22);
    int stW = TextWidth(dc, stTxt, g_fontCap) + SS(20);
    RECT stR{ ix, yy, ix + stW, yy + chipH };
    DrawChip(dc, stR, stTxt, sel.enabled ? kGreen : kDim, Tok::Surface2);
    if (probs > 0) {
        std::wstring prTxt = Str_Problems(probs);
        int prW = TextWidth(dc, prTxt, g_fontCap) + SS(28);
        DrawChip(dc, RECT{ stR.right + SS(8), yy, stR.right + SS(8) + prW, yy + chipH },
                 L"\u26A0 " + prTxt, kOrange, Tok::Surface2);
    }
    yy += chipH + SS(12);
    HPEN pen = CreatePen(PS_SOLID, 1, Tok::Divider);
    auto oldPen = SelectObject(dc, pen);
    MoveToEx(dc, ix, yy, nullptr); LineTo(dc, ix + iw, yy);
    SelectObject(dc, oldPen); DeleteObject(pen);

    yy += SS(14);
    DrawTextR(dc, sel.description, RECT{ ix, yy, ix + iw, yy + SS(80) }, g_fontBody, kDim, DT_LEFT | DT_WORDBREAK);

    yy += SS(96);
    DrawTextR(dc, probs > 0 ? (std::wstring(L"\u26A0 ") + Str_Problems(probs)) : (std::wstring(L"\u2713 ") + Str_NoProblems()),
              RECT{ ix, yy, ix + iw, yy + SS(24) }, g_fontBody, probs > 0 ? kOrange : kGreen,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    yy += SS(30);
    auto conflicts = DetectConflicts();
    bool hasConf = false;
    for (auto& c : conflicts) if (c.first == modIdx || c.second == modIdx) { hasConf = true; break; }
    if (hasConf) {
        DrawTextR(dc, Str_ConflictsTitle(), RECT{ ix, yy, ix + iw, yy + SS(22) }, g_fontH2, kOrange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        yy += SS(28);
        for (auto& c : conflicts) {
            if (c.first != modIdx && c.second != modIdx) continue;
            const ModEntry& other = (c.first == modIdx) ? g_mods[c.second] : g_mods[c.first];
            DrawTextR(dc, L"\u26A0 " + other.name, RECT{ ix, yy, ix + iw, yy + SS(24) }, g_fontBody, kDim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            yy += SS(26);
        }
        yy += SS(6);
    }

    // Action buttons (rects computed in RecalcLayout).
    // FIX: buttons 0/1 used to run the identical OpenModLua action under two
    // different labels ("Edit" vs "Open main.lua"). Now all three are distinct:
    // top = open folder, middle = open main.lua, bottom = enable/disable.
    const wchar_t* labels[3] = { sel.enabled ? Str_Disable() : Str_Enable(), Str_OpenLua(), Str_OpenFolder() };
    for (int k = 2; k >= 0; --k) {
        RECT b = g_geo.inspectorBtns[k];
        bool hovered = (g_hoverBtn == k);
        FillRoundRect(dc, b, hovered ? kHover : Tok::Surface2, Tok::RadiusBtn, Tok::Border, true);
        if (k == 0) {
            // Power glyph + left-aligned label for the toggle action.
            int isz = SS(15);
            int icy = (b.top + b.bottom) / 2;
            RECT gr{ b.left + SS(16), icy - isz / 2, b.left + SS(16) + isz, icy + isz / 2 };
            DrawGlyph(dc, gr, Glyph::Power, sel.enabled ? kOrange : kGreen);
            DrawTextR(dc, labels[k], RECT{ gr.right + SS(10), b.top, b.right - SS(10), b.bottom },
                      g_fontBody, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            DrawTextR(dc, labels[k], RECT{ b.left + SS(10), b.top, b.right - SS(10), b.bottom },
                      g_fontBody, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

void PaintMods(HDC dc) {
    int countAll = static_cast<int>(g_mods.size());
    int countVis = static_cast<int>(g_visible.size());
    std::wstring rightHint = std::to_wstring(countAll) + L" " + L10N(L"\u043C\u043E\u0434\u043E\u0432", L"mods");
    if (!g_searchQuery.empty())
        rightHint += L"  \u00B7  " + std::to_wstring(countVis) + L" " + L10N(L"\u043F\u043E\u043A\u0430\u0437\u0430\u043D\u043E", L"shown");
    PaintContentTitle(dc, Str_NavMods(), rightHint);

    RECT s = g_geo.search;
    FillRoundRect(dc, s, g_hoverSearch ? kHover : Tok::Surface, Tok::RadiusBtn,
                  g_searchFocused ? Tok::Accent : Tok::Border, true);
    if (!g_searchQuery.empty()) {
        DrawTextR(dc, g_searchQuery, RECT{ s.left + SS(12), s.top, s.right - SS(24), s.bottom },
                  g_fontBody, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, L"\u00D7", RECT{ s.right - SS(20), s.top, s.right - SS(4), s.bottom },
                  g_fontBody, g_hoverSearch ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        DrawTextR(dc, Str_SearchPh(), RECT{ s.left + SS(12), s.top, s.right - SS(12), s.bottom },
                  g_fontBody, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    RECT l = g_geo.list;
    int savedDC = SaveDC(dc);
    IntersectClipRect(dc, l.left, l.top, l.right, l.bottom);

    if (g_mods.empty()) {
        int cx = (l.left + l.right) / 2;
        int cyy = l.top + SS(96);
        DrawCircleOutline(dc, cx, cyy, SS(30), kFaint);
        DrawTextR(dc, L"?", RECT{ cx - SS(30), cyy - SS(30), cx + SS(30), cyy + SS(30) },
                  g_fontH1, kFaint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, Str_NoMods(), RECT{ l.left, cyy + SS(44), l.right, cyy + SS(70) },
                  g_fontH2, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, Str_NoModsHint(), RECT{ l.left, cyy + SS(76), l.right, cyy + SS(100) },
                  g_fontBody, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (g_visible.empty()) {
        int cx = (l.left + l.right) / 2;
        int cyy = l.top + SS(96);
        DrawCircleOutline(dc, cx, cyy, SS(30), kFaint);
        DrawTextR(dc, L"\u00D7", RECT{ cx - SS(30), cyy - SS(30), cx + SS(30), cyy + SS(30) },
                  g_fontH1, kFaint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, Str_NoResults(), RECT{ l.left, cyy + SS(44), l.right, cyy + SS(70) },
                  g_fontBody, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        int rowStep = RowStep();
        int rowH = SS(kRowH);
        int yPos = l.top + SS(4) - g_scroll;
        auto conflicts = DetectConflicts();
        for (size_t i = 0; i < g_visible.size(); ++i) {
            const ModEntry& m = g_mods[g_visible[i]];
            int idx = static_cast<int>(i);
            if (yPos + rowH >= l.top && yPos <= l.bottom) {
                RECT rc{ l.left, yPos, l.right - SS(kScrollW + 4), yPos + rowH };
                int probs = ProblemsForIndex(g_visible[i], conflicts);
                bool isDrag = g_dragState.dragging && g_dragState.dragIndex == idx;
                if (isDrag) {
                    RECT dr = rc; dr.left += SS(4); dr.top -= SS(2); dr.right += SS(2); dr.bottom += SS(2);
                    PaintRow(dc, dr, m, true, true, probs);
                } else {
                    PaintRow(dc, rc, m, (g_selected == idx), (g_hoverRow == idx) && !isDrag, probs);
                }
            }
            yPos += rowStep;
        }
    }
    RestoreDC(dc, savedDC);

    int listH = l.bottom - l.top;
    int totalH = static_cast<int>(g_visible.size()) * RowStep();
    if (totalH > listH) {
        int maxScroll = totalH - listH;
        int trackH = listH - SS(8);
        int trackX = l.right - kScrollW;
        int trackY = l.top + SS(4);
        FillRoundRect(dc, RECT{trackX, trackY, trackX + kScrollW, trackY + trackH}, Tok::ScrollTrack, 3);
        int thumbH = std::max(SS(20), trackH * listH / totalH);
        int thumbY = trackY + (maxScroll ? (g_scroll * (trackH - thumbH) / maxScroll) : 0);
        FillRoundRect(dc, RECT{trackX, thumbY, trackX + kScrollW, thumbY + thumbH}, Tok::ScrollThumb, 3);
    }

    // Inspector
    FillRoundRect(dc, g_geo.inspector, Tok::Surface, Tok::RadiusCard, Tok::Border, true);
    int selIdx = GlobalIndexFor(g_selected);
    if (selIdx >= 0) PaintInspector(dc, selIdx);
    else {
        RECT ins = g_geo.inspector;
        int cx = (ins.left + ins.right) / 2;
        int cyy = (ins.top + ins.bottom) / 2 - SS(20);
        DrawCircleOutline(dc, cx, cyy, SS(24), kFaint);
        DrawTextR(dc, L"i", RECT{ cx - SS(24), cyy - SS(24), cx + SS(24), cyy + SS(24) },
                  g_fontH2, kFaint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, Str_SelectHint(), RECT{ ins.left + SS(16), cyy + SS(32), ins.right - SS(16), cyy + SS(80) },
                  g_fontBody, kDim, DT_CENTER | DT_VCENTER);
    }

    // Bottom bar
    RECT bar{ g_geo.content.left + SS(24), l.bottom + SS(10), g_geo.content.right - SS(24), g_geo.applyBtn.bottom };
    HPEN pen2 = CreatePen(PS_SOLID, 1, Tok::Divider);
    auto oldPen2 = SelectObject(dc, pen2);
    MoveToEx(dc, bar.left, bar.top, nullptr); LineTo(dc, bar.right, bar.top);
    SelectObject(dc, oldPen2); DeleteObject(pen2);
    DrawTextR(dc, Str_ActiveCount(EnabledModCount(), static_cast<int>(g_mods.size())),
              RECT{ bar.left, bar.top, bar.left + SS(300), bar.bottom }, g_fontBody, kDim,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawButton(dc, g_geo.applyBtn, Str_ApplyBtn(), Tok::Accent, Tok::AccentHover,
               g_hoverApply, g_down && g_hoverApply, g_dirty, g_fontBody);
}

void PaintSettings(HDC dc) {
    PaintContentTitle(dc, Str_NavSettings(), L"");
    int pad = SS(24);
    int cx = g_geo.content.left + pad;
    int cw = (g_geo.content.right - g_geo.content.left) - pad * 2;

    // Language
    DrawTextR(dc, St_SettingsLang(), RECT{ cx, g_geo.langY, cx + cw, g_geo.langY + SS(26) }, g_fontH2, kFaint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT seg = g_geo.langSeg;
    FillRoundRect(dc, seg, Tok::Surface, Tok::RadiusPill);
    int half = (seg.right - seg.left) / 2;
    RECT ru{ seg.left, seg.top, seg.left + half + 2, seg.bottom };
    RECT en{ seg.left + half - 2, seg.top, seg.right, seg.bottom };
    RECT act = g_isRussian ? ru : en;
    FillRoundRect(dc, act, Tok::Surface2, Tok::RadiusPill - 2);
    DriveSeg(dc, act);
    DrawTextR(dc, L"RU", ru, g_fontBody, g_isRussian ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, L"EN", en, g_fontBody, g_isRussian ? kDim : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Game
    DrawTextR(dc, St_SettingsGame(), RECT{ cx, g_geo.gameY, cx + cw, g_geo.gameY + SS(26) }, g_fontH2, kFaint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, St_GamePath(), RECT{ cx, g_geo.gamePathY, cx + cw, g_geo.gamePathY + SS(22) }, g_fontCap, kFaint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, GetExeDirectory(), RECT{ cx, g_geo.gamePathY + SS(24), cx + cw, g_geo.gamePathY + SS(48) }, g_fontBody, kText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    GameInfo gi = ComputeGameInfo();
    DrawTextR(dc, std::wstring(St_Status()) + L": ", RECT{ cx, g_geo.gameStatusY, cx + SS(80), g_geo.gameStatusY + SS(24) },
              g_fontCap, kFaint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, gi.headline, RECT{ cx + SS(82), g_geo.gameStatusY, cx + cw, g_geo.gameStatusY + SS(24) },
              g_fontBody, gi.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Diagnostics
    DrawTextR(dc, St_SettingsDiag(), RECT{ cx, g_geo.diagY, cx + cw, g_geo.diagY + SS(26) }, g_fontH2, kFaint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawButton(dc, g_geo.diagBtn1, Str_OpenLogs(), Tok::Surface2, Tok::SurfaceHov,
               g_hoverD1, g_down && g_hoverD1, true, g_fontBody);
    DrawButton(dc, g_geo.diagBtn2, Str_OpenModsDir(), Tok::Surface2, Tok::SurfaceHov,
               g_hoverD2, g_down && g_hoverD2, true, g_fontBody);
    DrawButton(dc, g_geo.diagBtn3, Str_ReInjectBtn(), Tok::Inject, Tok::InjectHov,
               g_hoverD3, g_down && g_hoverD3, (!g_injecting && g_gamePid != 0), g_fontBody);

    // About
    RECT a = g_geo.aboutCard;
    FillRoundRect(dc, a, Tok::Surface, Tok::RadiusCard, Tok::Border, true);
    DrawTextR(dc, Str_VersionLine(), RECT{ a.left + SS(16), a.top + SS(14), a.right - SS(16), a.top + SS(40) },
              g_fontBody, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextR(dc, L"RA2 Yuri's Revenge v1.001 \u00B7 Lua 5.4", RECT{ a.left + SS(16), a.top + SS(42), a.right - SS(16), a.top + SS(66) },
              g_fontCap, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void PaintAll(HDC dc) {
    RECT full{ 0, 0, g_clientW, g_clientH };
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &full, bg);
    DeleteObject(bg);

    PaintSidebar(dc);
    if (g_view == View::Dashboard) PaintDashboard(dc);
    else if (g_view == View::Mods) PaintMods(dc);
    else PaintSettings(dc);

    if (g_toastActive) {
        int pad = SS(12);
        RECT tr{ g_geo.sidebar.right + pad, pad, g_clientW - pad, pad + SS(40) };
        FillRoundRect(dc, tr, kSurface2, SS(6), Tok::Border, true);
        DrawTextR(dc, g_toastText, RECT{ tr.left + SS(12), tr.top, tr.right - SS(12), tr.bottom },
                  g_fontBody, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_tipShow && !g_tipText.empty()) {
        int tw = TextWidth(dc, g_tipText, g_fontCap) + SS(22);
        int th = SS(28);
        int tx = g_tipPos.x + SS(16);
        int ty = g_tipPos.y + SS(24);
        if (tx + tw > g_clientW - SS(8)) tx = g_clientW - tw - SS(8);
        if (ty + th > g_clientH - SS(8)) ty = g_tipPos.y - th - SS(10);
        if (tx < SS(8)) tx = SS(8);
        if (ty < SS(8)) ty = SS(8);
        RECT tr{ tx, ty, tx + tw, ty + th };
        FillRoundRect(dc, tr, Tok::Surface2, SS(6), Tok::Border, true);
        DrawTextR(dc, g_tipText, RECT{ tr.left + SS(11), tr.top, tr.right - SS(11), tr.bottom },
                  g_fontCap, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
void OnLeftDown(POINT pt) {
    if (!(g_view == View::Mods && PointIn(g_geo.search, pt)))
        g_searchFocused = false;
    // Sidebar nav
    if (PointIn(g_geo.navDashboard, pt)) { SetView(View::Dashboard); return; }
    if (PointIn(g_geo.navMods, pt))      { SetView(View::Mods); return; }
    if (PointIn(g_geo.navSettings, pt))  { SetView(View::Settings); return; }

    // Search focus / clear
    if (g_view == View::Mods) {
        if (PointIn(g_geo.search, pt)) {
            if (!g_searchQuery.empty() && pt.x >= g_geo.search.right - SS(24)) {
                g_searchQuery.clear();
                g_searchFocused = true;
                RebuildVisible();
            }
            g_searchFocused = true;
            InvalidateRect(g_hwnd, nullptr, TRUE);
            return;
        }
        // Row quick actions (folder / lua)
        int actIdx = -1;
        int act = RowQuickAt(pt, &actIdx);
        if (act != 0) { if (act == 1) OpenModFolder(actIdx); else OpenModLua(actIdx); return; }
        // Row checkbox toggle
        int chkIdx = -1;
        if (RowCheckAt(pt, &chkIdx)) { ToggleModEnable(chkIdx); return; }
        // Inspector buttons: 0 = bottom (enable/disable), 1 = middle (main.lua), 2 = top (folder)
        for (int k = 0; k < 3; ++k) {
            if (PointIn(g_geo.inspectorBtns[k], pt)) {
                if (GlobalIndexFor(g_selected) < 0) return;
                if (k == 0) ToggleModEnable(g_selected);
                else if (k == 1) OpenModLua(g_selected);
                else OpenModFolder(g_selected);
                return;
            }
        }
        // Apply
        if (PointIn(g_geo.applyBtn, pt)) { SaveMods(); return; }
        // Row select / drag
        int idx = RowIndexAt(pt);
        if (idx >= 0) {
            SelectMod(idx);
            g_dragState.pendingClick = true;
            g_dragState.pendingIndex = idx;
            g_dragState.downPos = pt;
            g_dragState.dragging = false;
            g_dragState.dragIndex = -1;
            if (g_searchQuery.empty() || idx == g_selected) {}
            SetCapture(g_hwnd);
            return;
        }
        return;
    }

    if (g_view == View::Dashboard) {
        if (PointIn(g_geo.launchBtn, pt)) { if (ComputeGameInfo().canLaunch) DoLaunchGame(); return; }
        if (PointIn(g_geo.injectBtn, pt)) { if (ComputeGameInfo().canInject) DoInjectAttach(); return; }
        if (PointIn(g_geo.cncBtn, pt)) { if (!g_launching && !g_injecting) DoLaunchCnCNet(); return; }
        if (PointIn(g_geo.quickAction1, pt)) { OpenModsDir(); return; }
        if (PointIn(g_geo.quickAction2, pt)) { OpenLogs(); return; }
        if (PointIn(g_geo.quickAction3, pt)) { if (g_gamePid != 0 && !g_injecting) DoInjectAttach(); return; }
        if (PointIn(g_geo.quickAction4, pt)) { SetView(View::Settings); return; }
        return;
    }

    if (g_view == View::Settings) {
        if (PointIn(g_geo.langSeg, pt)) {
            g_isRussian = !g_isRussian;
            SavePrefs();
            InvalidateRect(g_hwnd, nullptr, TRUE);
            return;
        }
        if (PointIn(g_geo.diagBtn1, pt)) { OpenLogs(); return; }
        if (PointIn(g_geo.diagBtn2, pt)) { OpenModsDir(); return; }
        if (PointIn(g_geo.diagBtn3, pt)) { if (g_gamePid != 0 && !g_injecting) DoInjectAttach(); return; }
        return;
    }
}

void OnContextMenu(POINT screenPt) {
    if (g_view != View::Mods) return;
    POINT cpt = screenPt;
    ScreenToClient(g_hwnd, &cpt);
    int idx = RowIndexAt(cpt);
    if (idx < 0) return;
    SelectMod(idx);
    const ModEntry* m = ModFor(idx);
    if (!m) return;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, m->enabled ? Str_Disable() : Str_Enable());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, Str_OpenLua());
    AppendMenuW(menu, MF_STRING, 3, Str_Explorer());

    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case 1: ToggleModEnable(idx); break;
    case 2: OpenModLua(idx); break;
    case 3: { std::wstring d = ModDirFor(idx); if (!d.empty()) OpenPath(d, L"explore"); } break;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ToggleFullscreen + WndProc (global scope, calls into helpers above)
// ---------------------------------------------------------------------------
void ToggleFullscreen() {
    if (!g_hwnd) return;
    if (!g_fullscreen) {
        GetWindowRect(g_hwnd, &g_windowedRect);
        LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
        SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            const RECT& w = mi.rcWork;
            SetWindowPos(g_hwnd, HWND_TOP, w.left, w.top, w.right - w.left, w.bottom - w.top,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            SetWindowPos(g_hwnd, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        g_fullscreen = true;
    } else {
        LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
        SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) |
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
        SetWindowPos(g_hwnd, HWND_TOP, g_windowedRect.left, g_windowedRect.top,
                     g_windowedRect.right - g_windowedRect.left, g_windowedRect.bottom - g_windowedRect.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
        g_fullscreen = false;
    }
    RecalcLayout();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;  // FIX: CreateWindowEx sends WM_CREATE before it
                        // returns, so g_hwnd was still null here and WinDpi()
                        // fell back to system DPI for the first layout.
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        LoadPrefs();
        RECT rc; GetClientRect(hwnd, &rc);
        g_clientW = rc.right - rc.left; g_clientH = rc.bottom - rc.top;
        if (g_clientW == 0) g_clientW = kDefaultClientW;
        if (g_clientH == 0) g_clientH = kDefaultClientH;
        RecalcLayout();
        ScanMods();
        SetTimer(hwnd, kGamePollTimerId, 1000, nullptr);
        RefreshGameProcessState();
        return 0;
    }
    case WM_SIZE:
        g_clientW = LOWORD(lParam);
        g_clientH = HIWORD(lParam);
        RecalcLayout();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            // FIX: search/apply/inspector rects keep stale Mods coordinates
            // in other views (hand cursor over unrelated areas); langSeg is
            // clickable in Settings but never showed a hand.
            bool inMods = (g_view == View::Mods);
            bool inSettings = (g_view == View::Settings);
            bool interactive = PointIn(g_geo.navDashboard, pt) || PointIn(g_geo.navMods, pt) ||
                               PointIn(g_geo.navSettings, pt) || PointIn(g_geo.launchBtn, pt) ||
                               PointIn(g_geo.injectBtn, pt) || PointIn(g_geo.cncBtn, pt) ||
                               (inMods && PointIn(g_geo.applyBtn, pt)) ||
                               PointIn(g_geo.quickAction1, pt) || PointIn(g_geo.quickAction2, pt) ||
                               PointIn(g_geo.quickAction3, pt) || PointIn(g_geo.quickAction4, pt) ||
                               PointIn(g_geo.diagBtn1, pt) || PointIn(g_geo.diagBtn2, pt) ||
                               PointIn(g_geo.diagBtn3, pt) || (inMods && PointIn(g_geo.search, pt)) ||
                               (inSettings && PointIn(g_geo.langSeg, pt)) ||
                               (inMods && RowIndexAt(pt) >= 0);
            if (inMods) for (int k = 0; k < 3; ++k) if (PointIn(g_geo.inspectorBtns[k], pt)) interactive = true;
            if (interactive) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CHAR:
        if (g_searchFocused) {
            wchar_t c = static_cast<wchar_t>(wParam);
            if (c == 8) {
                if (!g_searchQuery.empty()) { g_searchQuery.pop_back(); RebuildVisible(); InvalidateRect(hwnd, nullptr, TRUE); }
            } else if (c >= 0x20 && c != 0x7F) {
                if (g_searchQuery.size() < 128) {
                    g_searchQuery.push_back(c); RebuildVisible(); InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_F11) { ToggleFullscreen(); return 0; }
        if (wParam == VK_ESCAPE) {
            if (g_searchFocused) { g_searchQuery.clear(); g_searchFocused = false; RebuildVisible(); InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (g_view == View::Mods && g_selected >= 0) { g_selected = -1; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (g_fullscreen) { ToggleFullscreen(); return 0; }
        }
        if (wParam == VK_RETURN && g_view == View::Mods && g_selected >= 0) { OpenModFolder(g_selected); return 0; }
        // FIX: the mod list was mouse-only. Arrows move selection, Space
        // toggles enable (not while typing in search), everything scrolls
        // into view.
        if (g_view == View::Mods && !g_visible.empty()) {
            if (wParam == VK_DOWN) {
                g_selected = (g_selected < 0) ? 0 : std::min(g_selected + 1, static_cast<int>(g_visible.size()) - 1);
                EnsureVisible(g_selected); InvalidateRect(hwnd, nullptr, TRUE); return 0;
            }
            if (wParam == VK_UP) {
                g_selected = (g_selected < 0) ? 0 : std::max(g_selected - 1, 0);
                EnsureVisible(g_selected); InvalidateRect(hwnd, nullptr, TRUE); return 0;
            }
            if (wParam == VK_SPACE && !g_searchFocused && g_selected >= 0) {
                ToggleModEnable(g_selected); return 0;
            }
        }
        if (GetKeyState(VK_CONTROL) < 0) {
            if (wParam == '1') { SetView(View::Dashboard); return 0; }
            if (wParam == '2') { SetView(View::Mods); return 0; }
            if (wParam == '3') { SetView(View::Settings); return 0; }
            if (wParam == 'F') { SetView(View::Mods); g_searchFocused = true; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            // FIX: custom-drawn search had no paste support.
            if (wParam == 'V' && g_searchFocused) {
                if (OpenClipboard(hwnd)) {
                    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                    if (hData) {
                        const wchar_t* clip = static_cast<const wchar_t*>(GlobalLock(hData));
                        if (clip) {
                            g_searchQuery += clip;
                            if (g_searchQuery.size() > 128) g_searchQuery.resize(128);
                            GlobalUnlock(hData);
                            RebuildVisible(); InvalidateRect(hwnd, nullptr, TRUE);
                        }
                    }
                    CloseClipboard();
                }
                return 0;
            }
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_dragState.pendingClick || g_dragState.dragging) {
            HideTooltip(hwnd);
            if (g_dragState.pendingClick) {
                long adx = pt.x - g_dragState.downPos.x; adx = adx < 0 ? -adx : adx;
                long ady = pt.y - g_dragState.downPos.y; ady = ady < 0 ? -ady : ady;
                if ((adx > SS(6) || ady > SS(6)) && g_searchQuery.empty()) {
                    g_dragState.dragging = true;
                    g_dragState.pendingClick = false;
                    g_dragState.dragIndex = g_dragState.pendingIndex;
                    g_dragState.dragAnchorY = pt.y;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            if (g_dragState.dragging) {
                int dy = pt.y - g_dragState.dragAnchorY;
                int step = RowStep();
                if (dy >= step / 2 && g_dragState.dragIndex + 1 < static_cast<int>(g_mods.size())) {
                    std::swap(g_mods[g_dragState.dragIndex], g_mods[g_dragState.dragIndex + 1]);
                    g_dragState.dragIndex += 1;
                    g_dragState.dragAnchorY += step;
                    InvalidateRect(hwnd, nullptr, TRUE);
                } else if (dy <= -step / 2 && g_dragState.dragIndex - 1 >= 0) {
                    std::swap(g_mods[g_dragState.dragIndex], g_mods[g_dragState.dragIndex - 1]);
                    g_dragState.dragIndex -= 1;
                    g_dragState.dragAnchorY -= step;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        // Hover: nav
        View hv = static_cast<View>(-1);
        if (PointIn(g_geo.navDashboard, pt)) hv = View::Dashboard;
        else if (PointIn(g_geo.navMods, pt)) hv = View::Mods;
        else if (PointIn(g_geo.navSettings, pt)) hv = View::Settings;
        if (hv != g_hoverNav) { g_hoverNav = hv; InvalidateRect(hwnd, nullptr, TRUE); }
        // Hover: buttons (dashboard/settings/mods)
        bool hL = g_view == View::Dashboard && PointIn(g_geo.launchBtn, pt);
        bool hI = g_view == View::Dashboard && PointIn(g_geo.injectBtn, pt);
        bool hC = g_view == View::Dashboard && PointIn(g_geo.cncBtn, pt);
        bool hA = g_view == View::Mods && PointIn(g_geo.applyBtn, pt);
        bool hQ1 = g_view == View::Dashboard && PointIn(g_geo.quickAction1, pt);
        bool hQ2 = g_view == View::Dashboard && PointIn(g_geo.quickAction2, pt);
        bool hQ3 = g_view == View::Dashboard && PointIn(g_geo.quickAction3, pt);
        bool hQ4 = g_view == View::Dashboard && PointIn(g_geo.quickAction4, pt);
        bool hD1 = g_view == View::Settings && PointIn(g_geo.diagBtn1, pt);
        bool hD2 = g_view == View::Settings && PointIn(g_geo.diagBtn2, pt);
        bool hD3 = g_view == View::Settings && PointIn(g_geo.diagBtn3, pt);
        if (hL != g_hoverLaunch || hI != g_hoverInject || hC != g_hoverCnc || hA != g_hoverApply ||
            hQ1 != g_hoverQA1 || hQ2 != g_hoverQA2 || hQ3 != g_hoverQA3 || hQ4 != g_hoverQA4 ||
            hD1 != g_hoverD1 || hD2 != g_hoverD2 || hD3 != g_hoverD3) {
            g_hoverLaunch = hL; g_hoverInject = hI; g_hoverCnc = hC; g_hoverApply = hA;
            g_hoverQA1 = hQ1; g_hoverQA2 = hQ2; g_hoverQA3 = hQ3; g_hoverQA4 = hQ4;
            g_hoverD1 = hD1; g_hoverD2 = hD2; g_hoverD3 = hD3;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        // Hover: search
        bool hS = PointIn(g_geo.search, pt);
        if (hS != g_hoverSearch) { g_hoverSearch = hS; InvalidateRect(hwnd, nullptr, TRUE); }
        // Hover: mods rows + inspector buttons
        if (g_view == View::Mods) {
            int row = RowIndexAt(pt);
            if (row != g_hoverRow) { g_hoverRow = row; InvalidateRect(hwnd, nullptr, TRUE); }
            int btn = -1;
            for (int k = 0; k < 3; ++k) if (PointIn(g_geo.inspectorBtns[k], pt)) { btn = k; break; }
            if (btn != g_hoverBtn) { g_hoverBtn = btn; InvalidateRect(hwnd, nullptr, TRUE); }
        } else {
            if (g_hoverRow != -1) { g_hoverRow = -1; InvalidateRect(hwnd, nullptr, TRUE); }
        }
        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_trackingMouse = true;
        }
        // Disabled-button tooltip ("why is this gray?"): arm a one-shot
        // timer; the tip appears only if the cursor rests on the button.
        {
            std::wstring reason = DisabledReason(pt);
            if (reason.empty()) {
                if (g_tipShow || !g_tipText.empty()) { HideTooltip(hwnd); InvalidateRect(hwnd, nullptr, TRUE); }
            } else if (reason != g_tipText) {
                g_tipText = reason;
                g_tipPos = pt;
                g_tipShow = false;
                KillTimer(hwnd, kTipTimerId);
                SetTimer(hwnd, kTipTimerId, 600, nullptr);
            } else {
                g_tipPos = pt;
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        g_hoverNav = static_cast<View>(-1);
        g_hoverLaunch = g_hoverInject = g_hoverCnc = g_hoverApply = false;
        g_hoverQA1 = g_hoverQA2 = g_hoverQA3 = g_hoverQA4 = false;
        g_hoverD1 = g_hoverD2 = g_hoverD3 = false;
        g_hoverSearch = false; g_hoverRow = -1; g_hoverBtn = -1;
        g_down = false;
        HideTooltip(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_MOUSEWHEEL: {
        HideTooltip(hwnd);
        if (g_view != View::Mods) return 0;  // FIX: scrolling on Dashboard /
                                             // Settings silently moved the
                                             // hidden Mods list offset.
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scroll -= delta * 40 / WHEEL_DELTA;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_down = true;
        HideTooltip(hwnd);
        OnLeftDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;
    case WM_LBUTTONUP: {
        g_down = false;
        bool pending = g_dragState.pendingClick;
        bool dragging = g_dragState.dragging;
        if (pending && !dragging) {
            int idx = g_dragState.pendingIndex;
            if (idx >= 0 && idx < static_cast<int>(g_visible.size())) {
                // already selected on down; no-op toggled via checkbox area
            }
        }
        if (dragging) {
            g_dirty = true;
            // FIX: keep the selection on the dragged mod instead of the row
            // it vacated (drag only starts unfiltered, so visible == global
            // indices here).
            g_selected = g_dragState.dragIndex;
        }
        g_dragState = DragState{};
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) g_dragState = DragState{};
        return 0;
    case WM_CONTEXTMENU: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        OnContextMenu(pt);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        if (g_view == View::Mods) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int idx = RowIndexAt(pt);
            if (idx >= 0 && RowCheckAt(pt, nullptr) == 0) { SelectMod(idx); OpenModFolder(idx); return 0; }
        }
        g_down = true;
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // FIX: a minimized window reports 0x0; CreateCompatibleBitmap would
        // fail and SelectObject(null) corrupts the paint cycle.
        if (g_clientW <= 0 || g_clientH <= 0) { EndPaint(hwnd, &ps); return 0; }
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, g_clientW, g_clientH);
        if (!mem || !bmp) {
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        auto oldBmp = SelectObject(mem, bmp);
        PaintAll(mem);
        BitBlt(hdc, 0, 0, g_clientW, g_clientH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = SS(kMinClientW);
        mmi->ptMinTrackSize.y = SS(kMinClientH);
        return 0;
    }
    case WM_TIMER:
        if (wParam == kToastTimerId) { KillTimer(hwnd, kToastTimerId); g_toastActive = false; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
        if (wParam == kTipTimerId) { KillTimer(hwnd, kTipTimerId); if (!g_tipText.empty()) { g_tipShow = true; InvalidateRect(hwnd, nullptr, TRUE); } return 0; }
        if (wParam == kGamePollTimerId) { RefreshGameProcessState(); return 0; }
        return 0;
    case WM_APP_LAUNCH_DONE: {
        g_launching = false;
        auto* res = reinterpret_cast<LaunchResult*>(lParam);
        bool ok = res ? res->ok : false;
        DWORD pid = res ? res->pid : 0;
        std::wstring nm = res ? res->name : L"";
        std::wstring err = res ? res->error : L"";
        delete res;
        if (ok && pid) {
            g_gamePid = pid;
            g_gameName = nm.empty() ? std::wstring(kGameProcess) : nm;
            g_injected = true;
            g_statusKey = StatusKey::Ready;
            ShowToast(L10N(L"\u2713 \u0418\u0433\u0440\u0430 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430 \u2014 LuaAPI \u0432\u043D\u0435\u0434\u0440\u0435\u043D\u0430", L"\u2713 Game running \u2014 LuaAPI Injected"));
        }
        else if (pid) {
            g_gamePid = pid;
            g_gameName = nm.empty() ? std::wstring(kGameProcess) : nm;
            g_injected = false;
            SetStatusKey(StatusKey::InjectFail);
            if (!err.empty())
                MessageBoxW(hwnd, (L"\u0418\u0433\u0440\u0430 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430, \u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + err).c_str(),
                            L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        }
        else { SetStatusKey(StatusKey::GameNotFound); }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_APP_INJECT_DONE: {
        g_injecting = false;
        auto* res = reinterpret_cast<InjectResult*>(lParam);
        bool ok = res ? res->ok : false;
        DWORD pid = res ? res->pid : 0;
        std::wstring err = res ? res->error : L"";
        delete res;
        if (ok && pid) {
            g_gamePid = pid; if (g_gameName.empty()) g_gameName = kGameProcess; g_injected = true;
            LogLine(L"Inject: complete — LuaAPI injected into PID " + std::to_wstring(pid));
            ShowToast(L10N(L"\u2713 LuaAPI \u0432\u043D\u0435\u0434\u0440\u0435\u043D\u0430 \u0432 \u0438\u0433\u0440\u0443", L"\u2713 LuaAPI injected"));
        } else {
            g_injected = false; SetStatusKey(StatusKey::InjectFail);
            LogLine(L"Inject: complete — FAILED (PID " + std::to_wstring(pid) + L"): " + err);
            MessageBoxW(hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + err).c_str(),
                        L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE:
        if (g_dirty) SaveMods();
        DestroyWindow(hwnd);
        return 0;
    case WM_DPICHANGED: {
        RECT* const prc = reinterpret_cast<RECT*>(lParam);
        RecreateFonts();
        g_clientW = prc->right - prc->left; g_clientH = prc->bottom - prc->top;
        RecalcLayout();
        SetWindowPos(hwnd, nullptr, prc->left, prc->top, g_clientW, g_clientH, SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kToastTimerId);
        KillTimer(hwnd, kTipTimerId);
        KillTimer(hwnd, kGamePollTimerId);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Headless diagnostic launch (--noinject) — preserved
// ---------------------------------------------------------------------------
int RunNoInjectDiagnostic() {
    std::wstring exeDir = GetExeDirectory();
    std::wstring gamePath = exeDir + L"\\gamemd.exe";
    if (!FileExists(gamePath)) {
        MessageBoxW(nullptr, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + gamePath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return 1;
    }
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(gamePath.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, exeDir.c_str(), &si, &pi)) return 1;
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    wchar_t b[16]; swprintf(b, 16, L"%08X", code);
    LogLine(std::wstring(L"Diagnostics: exited code=0x") + b);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // FIX: SetProcessDpiAwarenessContext lives in user32.dll, not shcore —
    // the old GetProcAddress(shcore, ...) always failed, silently dropping
    // back to system-DPI awareness despite the WM_DPICHANGED handler.
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        typedef HRESULT (WINAPI *SetDpiCtxFn)(HANDLE);
        SetDpiCtxFn setDPI = user32 ? reinterpret_cast<SetDpiCtxFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;
        if (setDPI) setDPI(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else SetProcessDPIAware();
    }

    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--noinject") == 0) { g_skipInjection = true; g_headless = true; }
            else if (_wcsicmp(argv[i], L"--withcncnet") == 0) { g_headless = true; g_runCnCNet = true; }
            else if (_wcsicmp(argv[i], L"--attach") == 0) {
                g_attachMode = true; g_headless = true;
                if (i + 1 < argc && argv[i + 1][0] != L'-') { g_attachTarget = argv[i + 1]; ++i; }
            } else if (_wcsnicmp(argv[i], L"--attach=", 9) == 0) {
                g_attachMode = true; g_headless = true; g_attachTarget = argv[i] + 9;
            }
        }
        if (argv) LocalFree(argv);

        if (!g_attachMode) {
            char envAttach[2] = {0};
            GetEnvironmentVariableA("LUAAPI_ATTACH", envAttach, sizeof(envAttach));
            if (envAttach[0] == '1') { g_attachMode = true; g_headless = true; }
            else { GetEnvironmentVariableA("ATTACH_MODE", envAttach, sizeof(envAttach)); if (envAttach[0] == '1') { g_attachMode = true; g_headless = true; } }
        }

        if (g_headless) {
            // FIX: the old code fired the async GUI launch and returned 500 ms
            // later, killing the detached worker before it could inject.
            // Headless paths are fully synchronous and block until exit.
            if (g_attachMode) return RunAttachWait(g_attachTarget);
            if (g_runCnCNet) return RunHeadlessCnCNet();
            return RunHeadlessLaunch();
        }
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.style = CS_DBLCLKS;
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);

    RECT rc{ 0, 0, kDefaultClientW, kDefaultClientH };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, FALSE, 0);
    int wndW = rc.right - rc.left;
    int wndH = rc.bottom - rc.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - wndW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - wndH) / 2;

    g_hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
                             x, y, wndW, wndH, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG,
        reinterpret_cast<LPARAM>(LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON))));
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL,
        reinterpret_cast<LPARAM>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR)));

    RecreateFonts();
    RecalcLayout();  // re-scale geometry for the real per-monitor DPI
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_fontTitle);
    DeleteObject(g_fontH1);
    DeleteObject(g_fontH2);
    DeleteObject(g_fontBody);
    DeleteObject(g_fontCap);
    DeleteObject(g_fontStat);
    return static_cast<int>(msg.wParam);
}
