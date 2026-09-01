// RA2 Yuri's Revenge — LuaAPI Injector (modern dark Win32 GUI, no console)
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

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// Глобальные функции, определённые ниже на уровне файла; видимы и из анонимного namespace.
void RecalcLayout();
void ToggleFullscreen();
// CardActionAt определён в анонимном namespace ниже; виден по всему translation unit.

namespace {

// ---------------------------------------------------------------------------
// DESIGN TOKENS — единый источник значений цвета/размера/типографики.
// Всё визуальное для лаунчера берётся ТОЛЬКО отсюда; ниже по файлу нет хардкода.
// ---------------------------------------------------------------------------
namespace Tok {
// Фон
constexpr COLORREF WindowBg    = RGB(14, 17, 22);     // окно      #0E1116
constexpr COLORREF Card        = RGB(22, 27, 34);     // карточка  #161B22
constexpr COLORREF CardHover   = RGB(28, 34, 43);     // hover     #1C222B
constexpr COLORREF Surface     = RGB(30, 37, 47);     // подложка пилюль/компакт-кнопок
constexpr COLORREF SurfaceHov  = RGB(46, 55, 68);
// Текст
constexpr COLORREF Text        = RGB(230, 237, 243);  // основной   #E6EDF3
constexpr COLORREF TextDim     = RGB(139, 148, 158);  // вторичный  #8B949E
// Акцент (включено / активно)
constexpr COLORREF Accent      = RGB(63, 185, 80);    // #3FB950
constexpr COLORREF AccentHover = RGB(91, 204, 111);
// Кнопки действий
constexpr COLORREF Launch     = RGB(218, 54, 51);     // #DA3633
constexpr COLORREF LaunchHov  = RGB(240, 84, 82);
constexpr COLORREF Inject     = RGB(31, 111, 235);    // #1F6FEB
constexpr COLORREF InjectHov  = RGB(76, 148, 242);
// Семантика статусов
constexpr COLORREF Warn        = RGB(236, 137, 36);
constexpr COLORREF Ok          = RGB(63, 185, 80);
constexpr COLORREF Error       = RGB(218, 54, 51);
// Muted / границы / скролл
constexpr COLORREF Chip        = RGB(48, 54, 64);
constexpr COLORREF Border      = RGB(58, 68, 80);
constexpr COLORREF Divider     = RGB(44, 52, 63);
constexpr COLORREF Disabled    = RGB(52, 60, 72);
constexpr COLORREF ScrollTrack = RGB(36, 43, 53);
constexpr COLORREF ScrollThumb = RGB(88, 97, 108);
// Радиусы
constexpr int RadiusCard = 8;
constexpr int RadiusBtn  = 6;
constexpr int RadiusPill = 12;
// Сетка отступов (8 / 12 / 16 / 24 / 32 / 40)
constexpr int S8  = 8;
constexpr int S12 = 12;
constexpr int S16 = 16;
constexpr int S24 = 24;
constexpr int S32 = 32;
constexpr int S40 = 40;
// Типографика (pt; используется с MulDiv(pt, dpi, 72))
constexpr int FontTitle = 20;
constexpr int FontCard  = 14;
constexpr int FontBody  = 12;
constexpr int FontSmall = 11;
// Прочее
constexpr int HoverMs = 120;   // плавный переход hover-подсветки, мс
} // namespace Tok

// Псевдонимы токенов — нижележащий код опирается только на эти значения.
constexpr COLORREF kBg        = Tok::WindowBg;
constexpr COLORREF kSurface   = Tok::Card;
constexpr COLORREF kSurfaceHov= Tok::SurfaceHov;
constexpr COLORREF kHover     = Tok::CardHover;
constexpr COLORREF kRed       = Tok::Launch;
constexpr COLORREF kBlue      = Tok::Inject;
constexpr COLORREF kGreen     = Tok::Accent;
constexpr COLORREF kGreenHover= Tok::AccentHover;
constexpr COLORREF kText      = Tok::Text;
constexpr COLORREF kDim       = Tok::TextDim;
constexpr COLORREF kBadge     = Tok::Chip;
constexpr COLORREF kOrange    = Tok::Warn;
constexpr COLORREF kOk        = Tok::Ok;

constexpr const wchar_t* kWindowClass = L"LuaAPIInjectorWnd";
constexpr const wchar_t* kWindowTitle = L"RA2 Yuri's Revenge - LuaAPI Engine";

constexpr int kDefaultClientW = 580;
constexpr int kDefaultClientH = 640;
constexpr int kPad = Tok::S24;        // внешний отступ окна (сетка 8 → 24)
constexpr int kMaxContentWidth = 1200; // cap контентной области; шире — центрируем
constexpr int kCardH = 80;            // высота карточки
constexpr int kCardGap = Tok::S8;     // отступ между карточками (8)
constexpr int kCardStep = kCardH + kCardGap;
constexpr int kCardInner = Tok::S16;  // внутренний отступ карточки (16)
constexpr int kScrollW = 6;           // тонкий скроллбар

constexpr const wchar_t* kGameProcess = L"gamemd.exe";

// ---------------------------------------------------------------------------
// Localization (RU / EN)
// ---------------------------------------------------------------------------
bool g_isRussian = true;

const wchar_t* L10N(const wchar_t* ru, const wchar_t* en) {
    return g_isRussian ? ru : en;
}

const wchar_t* Str_Subtitle() {
    return L10N(L"Yuri's Revenge v1.001 Modding Platform",
                L"Yuri's Revenge v1.001 Modding Platform");
}
const wchar_t* Str_StatusReady() {
    return L10N(L"Готов к запуску",
                L"Ready to Launch");
}
const wchar_t* Str_StatusInjected() { return L10N(
    L"Игра запущена & LuaAPI внедрена",
    L"Game Running & LuaAPI Injected"); }
const wchar_t* Str_Busy() { return L10N(
    L"Работает...",
    L"Working..."); }
const wchar_t* Str_LaunchBtn() { return L10N(
    L"Запустить игру",
    L"Launch Game"); }
const wchar_t* Str_InjectBtn() { return L10N(
    L"Внедрить",
    L"Inject"); }
const wchar_t* Str_ModsHeader() { return L10N(
    L"МОДЫ",
    L"INSTALLED MODS"); }
const wchar_t* Str_SaveBtn() { return L10N(
    L"Сохранить и применить",
    L"Save Apply"); }
std::wstring Str_ActiveCount(int active, int total) {
    return g_isRussian
        ? L"\u0410\u043A\u0442\u0438\u0432\u043D\u043E: " + std::to_wstring(active) + L" \u0438\u0437 " + std::to_wstring(total)
        : L"Active: " + std::to_wstring(active) + L" of " + std::to_wstring(total);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class AppState { Ready, Busy, StatusError, Injected };

HWND g_hwnd = nullptr;
HFONT g_fontTitle = nullptr;   // заголовок окна (20pt bold)
HFONT g_fontCard  = nullptr;   // заголовок карточки (14pt semibold)
HFONT g_fontBody  = nullptr;   // описание/основной текст (12pt)
HFONT g_fontSmall = nullptr;   // мелкие подписи / бейдж (11pt)

// Текущий DPI окна (GetDpiForWindow, fallback на LOGPIXELSX).
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

// Создаёт шрифт Segoe UI заданного pt-размера (точки → логические единицы через DPI).
HFONT CreateFontToken(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, WinDpi(), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// Пересоздаёт все шрифты из токенов (при старте и при смене DPI).
void RecreateFonts() {
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontCard)  DeleteObject(g_fontCard);
    if (g_fontBody)  DeleteObject(g_fontBody);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    g_fontTitle = CreateFontToken(Tok::FontTitle, FW_BOLD);
    g_fontCard  = CreateFontToken(Tok::FontCard,  FW_SEMIBOLD);
    g_fontBody  = CreateFontToken(Tok::FontBody,  FW_NORMAL);
    g_fontSmall = CreateFontToken(Tok::FontSmall, FW_NORMAL);
}

DWORD g_gamePid = 0;
bool g_skipInjection = false;
bool g_injectCnCNet = false;
bool g_attachMode = false;
std::wstring g_attachTarget;   // --attach NNN.exe: явная цель (переопределяет дефолты)
std::wstring g_gameName;
AppState g_appState = AppState::Ready;
bool g_dirty = false;
bool g_launching = false;
bool g_injecting = false;
constexpr UINT WM_APP_LAUNCH_DONE = WM_APP + 1;
constexpr UINT WM_APP_INJECT_DONE = WM_APP + 2;
constexpr UINT kToastTimerId = 1;
constexpr UINT kHoverTimerId = 2;   // анимация hover-подсветки карточек (плавный переход)

// Translatable status: store the KEY, localize at paint time so the
// RU/EN switch instantly re-translates even previously shown statuses.
enum class StatusKey { Ready, Injected, NotFound, DllMissing, InjectFail, GameNotFound, BusyLaunch, BusyInject, SaveFail, Custom };
StatusKey g_statusKey = StatusKey::Ready;
std::wstring g_statusCustom;
COLORREF g_statusColor = kOk;

int g_clientW = kDefaultClientW;
int g_clientH = kDefaultClientH;
int g_scroll = 0;

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

std::vector<ModEntry> g_mods;

// Построение видимого (отфильтрованного) подмножества модов; см. определение ниже.
void RebuildVisible();

RECT g_rcLaunch{};
RECT g_rcInject{};
RECT g_rcSave{};
RECT g_rcLang{};
bool g_hoverLaunch = false;
bool g_hoverInject = false;
bool g_hoverSave = false;
bool g_hoverLang = false;
bool g_trackingMouse = false;
// Hover-состояние кнопок быстрого доступа на карточке мода (0=нет, 1=папка, 2=карандаш).
int g_hoverActionCard = -1;
int g_hoverActionBtn  = 0;
// Плавная анимация подсветки карточки (pressed-состояние кнопок).
int   g_hoverCardIdx = -1;   // карточка под курсором (для hover-анимации)
float g_hoverFade    = 0.f;  // 0..1 — текущая фаза перехода
bool  g_down         = false; // ЛКМ удерживается (для pressed-вида кнопок)
bool  g_hoverWasCard = false; // был ли зафиксирован карточный hover (для плавного выхода)
// Тултипы кнопок быстрого доступа.
std::wstring g_tooltipText;
RECT g_tooltipAnchor{0,0,0,0};
bool g_tooltipVisible = false;
// Поиск/фильтр по модам.
RECT g_rcSearch{};
bool g_searchFocused = false;
bool g_hoverSearch = false;
std::wstring g_searchQuery;
std::vector<int> g_visible;   // индексы в g_mods, прошедшие фильтр (пусто = всё)
bool g_headless = false;
bool g_fullscreen = false;
RECT g_windowedRect{};  // исходная геометрия окна для возврата из полного экрана

// RecCalcLayout/ToggleFullscreen определены ниже на уровне файла (глобально).

// Drag-and-drop reorder state for the mod cards.
struct DragState {
    bool pendingClick = false;   // mouse-down on a card, not yet decided click vs drag
    bool dragging = false;       // drag in progress (moved > 6px)
    int pendingIndex = -1;       // card index where mouse-down happened
    int dragIndex = -1;          // current index of the dragged card
    POINT downPos{0, 0};         // client coords of the mouse-down
    int dragAnchorY = 0;         // client Y anchor for step-based swapping
};
DragState g_dragState;

// Worker-thread result for async injection, posted to the UI thread via WM_APP_INJECT_DONE.
struct InjectResult {
    bool ok = false;
    DWORD pid = 0;
    std::wstring error;
};

// Layout centralization - single source of truth
struct Layout {
    RECT launch, inject, lang, search, save, list;
    int footerTop, footerBottom, bannerTop, bannerBottom;
};
Layout ComputeLayout(int w, int h) {
    Layout l{};
    // Контентная область ограничена сверху kMaxContentWidth и центрируется на широких
    // окнах; на окнах ≤1200px работает прежнее растяжение с отступами по краям.
    int effW = std::min(w, kMaxContentWidth);
    int xOffset = (w - effW) / 2;

    int btnWidth = (effW - kPad * 2 - 12) / 2;
    l.launch = RECT{ xOffset + kPad, 96, xOffset + kPad + btnWidth, 140 };
    l.inject = RECT{ xOffset + kPad + btnWidth + 12, 96, xOffset + effW - kPad, 140 };
    l.lang   = RECT{ xOffset + effW - 110, 16, xOffset + effW - 20, 44 };
    // Заголовок секции и список — строго под кнопкой Launch (не перекрывают кнопки).
    int sectionBottom = l.launch.bottom + 36; // y=176 при дефолте
    // Поле поиска — на строке заголовка секции, справа.
    int secTop = l.launch.bottom + 16;
    int secBot = l.launch.bottom + 36;
    l.search = RECT{ xOffset + effW - kPad - 250, secTop, xOffset + effW - kPad, secBot };

    // Футер — единая планка у нижнего края окна (sock: сетка 8, высота 56).
    constexpr int kFooterH = 56;
    l.footerTop   = h - kPad - kFooterH;
    l.footerBottom = h - kPad;

    // Список заполняет ВСЁ пространство от заголовка SECTION до футера (без мёртвой зоны).
    l.list = RECT{ xOffset + kPad, sectionBottom + Tok::S8,
                   xOffset + effW - kPad, l.footerTop - Tok::S8 };

    // Баннер конфликтов — сразу над футер-планкой.
    l.bannerTop    = l.footerTop - 24;
    l.bannerBottom = l.footerTop;

    // Кнопка Save — справа внутри футер-планки, по вертикальному центру.
    constexpr int kSaveH = 38;
    int saveTop = l.footerTop + (kFooterH - kSaveH) / 2;
    l.save = RECT{ xOffset + effW - kPad - 220, saveTop,
                   xOffset + effW - kPad, saveTop + kSaveH };
    return l;
}
RECT ListRect() {
    return ComputeLayout(g_clientW, g_clientH).list;
}
inline void ClampScroll() {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    int listHeight = l.list.bottom - l.list.top;
    int totalModHeight = static_cast<int>(g_visible.size()) * kCardStep;
    int maxScroll = std::max(0, totalModHeight - listHeight);
    g_scroll = std::max(0, std::min(g_scroll, maxScroll));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void SetStatusKey(StatusKey key) {
    g_statusKey = key;
    g_statusCustom.clear();
    if (g_hwnd)
        InvalidateRect(g_hwnd, nullptr, TRUE);
}

void SetStatusCustom(const std::wstring& text) {
    g_statusKey = StatusKey::Custom;
    g_statusCustom = text;
    if (g_hwnd)
        InvalidateRect(g_hwnd, nullptr, TRUE);
}

std::wstring CurrentStatusText() {
    switch (g_statusKey) {
    case StatusKey::Ready:        return Str_StatusReady();
    case StatusKey::Injected:     return Str_StatusInjected();
    case StatusKey::NotFound:     return L10N(L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D\u0430",
                                              L"Game not found");
    case StatusKey::DllMissing:   return std::wstring(L"LuaAPI.dll ") +
                                       L10N(L"\u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D", L"not found");
    case StatusKey::InjectFail:   return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C",
                                              L"Injection failed");
    case StatusKey::GameNotFound: return std::wstring(L"gamemd.exe ") +
                                       L10N(L"\u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D", L"not found");
    case StatusKey::BusyLaunch:   return L10N(L"\u0417\u0430\u043F\u0443\u0441\u043A gamemd.exe...",
                                              L"Launching gamemd.exe...");
    case StatusKey::BusyInject:   return L10N(L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435...",
                                              L"Injecting...");
    case StatusKey::SaveFail:     return L10N(L"\u041E\u0448\u0438\u0431\u043A\u0430 \u0437\u0430\u043F\u0438\u0441\u0438 active_mods.txt",
                                              L"Failed to write active_mods.txt");
    default:                      return g_statusCustom;
    }
}

COLORREF CurrentStatusColor() {
    switch (g_statusKey) {
    case StatusKey::Ready:
    case StatusKey::Injected:   return kOk;
    case StatusKey::BusyLaunch:
    case StatusKey::BusyInject: return kOrange;
    default:                    return g_appState == AppState::StatusError ? kRed : kDim;
    }
}

void SetStatus(const std::wstring& text) { SetStatusCustom(text); }

void FillRoundRect(HDC dc, const RECT& r, COLORREF fill, int radius, COLORREF outline = 0, bool hasOutline = false) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = hasOutline ? CreatePen(PS_SOLID, 1, outline)
                          : reinterpret_cast<HPEN>(GetStockObject(NULL_PEN));
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    if (hasOutline)
        DeleteObject(pen);
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
    // DT_NOPREFIX: `&` в метках выводится буквально, а не как mnemonic (иначе «Save Apply» даёт «Save _Apply»).
    DrawTextW(dc, text.c_str(), -1, &rc, flags | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old);
}

// Две маленькие кнопки быстрого доступа справа на карточке мода: папка (открыть
// директорию мода) и карандаш (открыть main.lua). Ректы вычисляются из ректа карточки.
void CardActionButtons(const RECT& rcCard, RECT* folder, RECT* pencil) {
    constexpr int kIconW = 26;
    constexpr int kGap = 6;
    constexpr int kPadR = 12;
    pencil->right = rcCard.right - kPadR;
    pencil->left  = pencil->right - kIconW;
    folder->right = pencil->left - kGap;
    folder->left  = folder->right - kIconW;
    int cy = (rcCard.top + rcCard.bottom) / 2;
    folder->top = pencil->top = cy - kIconW / 2;
    folder->bottom = pencil->bottom = cy + kIconW / 2;
}

// Маленькая залитая иконка папки в прямоугольнике r цветом color.
void DrawFolderIcon(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left + 2, r.top + 6, r.right - 2, r.bottom - 2, 3, 3); // корпус
    Rectangle(dc, r.left + 2, r.top + 3, r.left + 10, r.top + 8);          // язычок (выступает над корпусом)
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Маленькая залитая иконка карандаша в прямоугольнике r цветом color.
void DrawPencilIcon(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    POINT body[4] = {                                  // диагональное тело
        {r.left + 3,  r.bottom - 4},
        {r.left + 7,  r.bottom - 8},
        {r.right - 8, r.top + 6},
        {r.right - 4, r.top + 2}
    };
    Polygon(dc, body, 4);
    POINT tip[3] = {                                   // остриё сверху-справа
        {r.right - 2, r.top},
        {r.right - 10, r.top + 1},
        {r.right - 5, r.top + 6}
    };
    Polygon(dc, tip, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Стилизованный чекбокс: при включении — заливка акцентом (#3FB950) + галочка,
// при выключении — контурная рамка. Никакого стандартного квадрата.
void DrawCheckbox(HDC dc, const RECT& box, bool enabled) {
    if (enabled) {
        FillRoundRect(dc, box, Tok::Accent, 5);
        HPEN pen = CreatePen(PS_SOLID, 2, Tok::Text);
        auto oldPen = SelectObject(dc, pen);
        MoveToEx(dc, box.left + 4,  box.top + 10, nullptr);
        LineTo(dc,   box.left + 8,  box.top + 14);
        LineTo(dc,   box.left + 16, box.top + 5);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    } else {
        FillRoundRect(dc, box, Tok::Card, 5, Tok::Border, true);
    }
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
        if (len == 0)
            return L".";
        if (len < path.size() - 1 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            break;
        path.resize(path.size() * 2);
    }
    path.resize(len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring GetPrefsPath() { return GetExeDirectory() + L"\\injector.ini"; }
void LoadPrefs() {
    wchar_t buf[16]={0};
    GetPrivateProfileStringW(L"UI", L"lang", L"RU", buf, 16, GetPrefsPath().c_str());
    g_isRussian = (_wcsicmp(buf, L"EN") != 0);
}
void SavePrefs() {
    WritePrivateProfileStringW(L"UI", L"lang", g_isRussian ? L"RU" : L"EN", GetPrefsPath().c_str());
}
void ShowToast(const std::wstring& msg) {
    SetStatusCustom(msg);
    if (g_hwnd) {
        KillTimer(g_hwnd, kToastTimerId);
        SetTimer(g_hwnd, kToastTimerId, 2000, nullptr);
    }
}

void LogLine(const std::wstring& text) {
    std::wofstream log(GetExeDirectory() + L"\\injector_log.txt",
                       std::ios::app);
    SYSTEMTIME st;
    GetLocalTime(&st);
    log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"."
        << st.wMilliseconds << L"] " << text << L"\n";
}
DWORD FindTargetProcess() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kGameProcess) != 0)
                continue;
            HANDLE moduleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, entry.th32ProcessID);
            if (moduleSnap != INVALID_HANDLE_VALUE) {
                MODULEENTRY32W mod{};
                mod.dwSize = sizeof(mod);
                if (Module32FirstW(moduleSnap, &mod) &&
                    _wcsicmp(mod.szModule, kGameProcess) == 0) {
                    pid = entry.th32ProcessID;
                }
                CloseHandle(moduleSnap);
            }
            if (pid)
                break;
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

bool InjectDllIntoProcess(DWORD pid, const std::wstring& dllPath, std::wstring* error) {
    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!process) {
        if (error) *error = L"OpenProcess failed (error " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    // Unicode: use LoadLibraryW + wchar_t buffer to support Cyrillic install paths
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteBase = VirtualAllocEx(process, nullptr, bytes,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBase) {
        if (error) *error = L"VirtualAllocEx failed (error " + std::to_wstring(GetLastError()) + L")";
        CloseHandle(process);
        return false;
    }

    if (!WriteProcessMemory(process, remoteBase, dllPath.c_str(), bytes, nullptr)) {
        if (error) *error = L"WriteProcessMemory failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remoteBase, 0, nullptr);
    if (!thread) {
        if (error) *error = L"CreateRemoteThread failed (error " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(thread, 5000);
    if (waitResult == WAIT_TIMEOUT) {
        // Не считаем это успехом и не блокируем окно: target не ответил на LoadLibraryW.
        if (error) *error = L10N(
            L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u0437\u0430\u0432\u0438\u0441\u043B\u043E: \u0446\u0435\u043B\u0435\u0432\u043E\u0439 \u043F\u0440\u043E\u0446\u0435\u0441\u0441 \u043D\u0435 \u0437\u0430\u0433\u0440\u0443\u0437\u0438\u043B DLL \u0437\u0430 5000 \u043C\u0441",
            L"Injection timed out: target did not load DLL within 5000 ms (WAIT_TIMEOUT)");
        CloseHandle(thread);
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
    CloseHandle(process);

    if (exitCode == 0) {
        if (error) *error = L"LoadLibraryW returned NULL inside the target";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Attach-режим: хелперы поиска процесса/модулей и чтения живой памяти.
// ---------------------------------------------------------------------------

// Ванильные пролог-байты (первые 8) целевых функций — для сверки живых байт
// перед инъектом (см. Gate 11.1): 0x55D360 = MainLoop, 0x734E60 = LoadString.
constexpr uintptr_t kSigAddrMainLoop = 0x0055D360;
constexpr uintptr_t kSigAddrLoadString = 0x00734E60;
const uint8_t kSigMainLoop[8]   = {0xA0, 0xA0, 0xE9, 0xA8, 0x00, 0x81, 0xEC, 0xB4};
const uint8_t kSigLoadString[8] = {0x53, 0x56, 0x8B, 0xF2, 0x8B, 0xD9, 0x85, 0xF6};

// Найти PID процесса по имени исполняемого файла (без учёта регистра).
DWORD FindProcessByName(const wchar_t* exeName) {
    if (!exeName || !*exeName) return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    DWORD pid = 0;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, exeName) == 0) { pid = e.th32ProcessID; break; }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return pid;
}

// Базовый адрес модуля заданного имени внутри процесса (или 0).
uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName) {
    uintptr_t base = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m{}; m.dwSize = sizeof(m);
    if (Module32FirstW(snap, &m)) {
        do {
            if (_wcsicmp(m.szModule, moduleName) == 0) {
                base = reinterpret_cast<uintptr_t>(m.modBaseAddr);
                break;
            }
        } while (Module32NextW(snap, &m));
    }
    CloseHandle(snap);
    return base;
}

std::vector<std::wstring> GetProcessModules(DWORD pid) {
    std::vector<std::wstring> mods;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return mods;
    MODULEENTRY32W m{}; m.dwSize = sizeof(m);
    if (Module32FirstW(snap, &m)) {
        do { mods.emplace_back(m.szModule); } while (Module32NextW(snap, &m));
    }
    CloseHandle(snap);
    return mods;
}

// Прочитать живую память из удалённого процесса.
bool ReadLiveBytes(DWORD pid, uintptr_t addr, uint8_t* buf, size_t n) {
    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) return false;
    SIZE_T read = 0;
    bool ok = ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addr), buf, n, &read) && read == n;
    CloseHandle(proc);
    return ok;
}

std::wstring BytesToHexStr(const uint8_t* bytes, size_t n) {
    wchar_t b[8];
    std::wstring out;
    for (size_t i = 0; i < n; ++i) {
        swprintf(b, 8, L"%02X", bytes[i]);
        out += b;
        if (i + 1 < n) out += L' ';
    }
    return out;
}

std::wstring HexWord(uintptr_t v) {
    wchar_t b[16];
    swprintf(b, 16, L"0x%08X", static_cast<unsigned int>(v));
    return b;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------


void DoFindGame() {
    g_gamePid = FindTargetProcess();
    g_gameName = g_gamePid ? kGameProcess : L"";
    if (g_gamePid == 0) {
        SetStatusKey(StatusKey::NotFound);
        MessageBoxW(g_hwnd,
                    L"\u0418\u0433\u0440\u0430 \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u0430!\n\n"
                    L"\u0421\u043D\u0430\u0447\u0430\u043B\u0430 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 Yuri's Revenge (gamemd.exe).",
                    L"\u041F\u043E\u0438\u0441\u043A \u043F\u0440\u043E\u0446\u0435\u0441\u0441\u0430", MB_ICONWARNING | MB_OK);
    } else {
        SetStatusCustom(std::wstring(L"Target: gamemd.exe (PID: ") + std::to_wstring(g_gamePid) + L")");
    }
}

void DoInject() {
    if (g_gamePid == 0)
        DoFindGame();

    if (g_gamePid == 0)
        return;

    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    std::wstring error;
    if (InjectDllIntoProcess(g_gamePid, dllPath, &error)) {
        SetStatusKey(StatusKey::Injected);
        MessageBoxW(g_hwnd,
                    L"LuaAPI.dll \u0443\u0441\u043F\u0435\u0448\u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0435\u043D \u0432 \u0438\u0433\u0440\u0443!",
                    L"\u0423\u0441\u043F\u0435\u0445", MB_ICONINFORMATION | MB_OK);
    } else {
        SetStatusKey(StatusKey::InjectFail);
        MessageBoxW(g_hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + error).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
    }
}
void DoLaunchGameAsync(HWND hwnd);

void DoLaunchGame() {
    if (g_launching) return;
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }
    DWORD existing = FindTargetProcess();
    if (existing != 0) {
        g_gamePid = existing;
        g_gameName = kGameProcess;
        std::wstring error;
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, existing);
        if (!process) { SetStatus(L"OpenProcess failed"); return; }
        bool ok = InjectDllIntoProcess(existing, dllPath, &error);
        CloseHandle(process);
        if (ok) { g_appState = AppState::Injected; SetStatusKey(StatusKey::Injected); }
        else { g_appState = AppState::StatusError; SetStatusKey(StatusKey::InjectFail); }
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }
    // Async path: spawn thread, disable UI
    g_launching = true;
    g_appState = AppState::Busy;
    SetStatusKey(StatusKey::BusyLaunch);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    HWND hwnd = g_hwnd;
    std::thread([hwnd]() { DoLaunchGameAsync(hwnd); }).detach();
}

void DoLaunchGameAsync(HWND hwnd) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    std::wstring stubPath = exeDir + L"\\RA2MD.EXE";
    if (FileExists(stubPath)) {
        LogLine(L"Launching via RA2MD.EXE stub...");
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(stubPath.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else {
            LogLine(L"Stub launch failed, falling back to direct spawn");
        }
    }
    bool injected = false;
    std::wstring err;
    DWORD foundPid = 0;
    for (int i = 0; i < 600; ++i) {
        Sleep(200);
        DWORD pid = FindTargetProcess();
        if (!pid) continue;
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process) continue;
        bool ok = InjectDllIntoProcess(pid, dllPath, &err);
        CloseHandle(process);
        if (ok) { foundPid = pid; injected = true; LogLine(L"Injected into freshly spawned gamemd.exe"); }
        else { foundPid = pid; }
        break;
    }
    PostMessageW(hwnd, WM_APP_LAUNCH_DONE, (WPARAM)injected, (LPARAM)foundPid);
}

void DoInjectAttachAsync(HWND hwnd, DWORD pid, const std::wstring& dllPath) {
    std::wstring error;
    bool ok = InjectDllIntoProcess(pid, dllPath, &error);
    PostMessageW(hwnd, WM_APP_INJECT_DONE, 0,
                 reinterpret_cast<LPARAM>(new InjectResult{ ok, pid, error }));
}

void DoInjectAttach() {
    if (g_injecting) return;

    DWORD pid = FindTargetProcess();
    if (pid == 0) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::NotFound);
        MessageBoxW(g_hwnd,
                    L"gamemd.exe \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D.\n\n"
                    L"\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0438\u0433\u0440\u0443 \u043A\u043D\u043E\u043F\u043A\u043E\u0439 \u00AB\u0417\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u044C \u0438\u0433\u0440\u0443\u00BB.",
                    L"\u041F\u043E\u0438\u0441\u043A \u043F\u0440\u043E\u0446\u0435\u0441\u0441\u0430", MB_ICONWARNING | MB_OK);
        return;
    }

    std::wstring dllPath = GetExeDirectory() + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::DllMissing);
        MessageBoxW(g_hwnd, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    g_gamePid = pid;
    g_injecting = true;
    g_appState = AppState::Busy;
    SetStatusKey(StatusKey::BusyInject);
    InvalidateRect(g_hwnd, nullptr, TRUE);

    // Async: работа внедрения уходит в отдельный поток, результат возвращается через
    // WM_APP_INJECT_DONE, чтобы окно не замерзало (InjectDllIntoProcess внутри имеет таймаут).
    HWND hwnd = g_hwnd;
    std::thread([hwnd, pid, dllPath]() { DoInjectAttachAsync(hwnd, pid, dllPath); }).detach();
}

// ---------------------------------------------------------------------------
// Wait & Attach (--attach / LUAAPI_ATTACH=1)
// Клиент CnCNet запускает игру сам (gamemd-spawn.exe через Syringe.exe).
// Этот режим НЕ запускает игру: поллит список процессов каждые 500мс до 120с,
// дожидаясь целевого exe, потом ждёт Ares/Phobos/CnCNet-Spawner.dll в модулях
// (после Syringe-инъекций), пауза 1с, сверяет живые байты на 0x55D360/0x734E60
// и только затем инжектит LuaAPI.dll. Блокирующий, headless: без окна.
// ---------------------------------------------------------------------------
int RunAttachWait(const std::wstring& explicitName) {
    LogLine(L"--- Attach mode (--attach / LUAAPI_ATTACH=1): waiting for game process ---");

    std::wstring exeDir = GetExeDirectory();
    std::wstring dllPath = exeDir + L"\\LuaAPI.dll";
    if (!FileExists(dllPath)) {
        LogLine(L"Attach: LuaAPI.dll not found: " + dllPath);
        MessageBoxW(nullptr, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + dllPath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Целевые имена: явный аргумент переопределяет дефолты (gamemd-spawn -> gamemd).
    std::vector<std::wstring> targets;
    if (!explicitName.empty()) {
        targets.push_back(explicitName);
    } else {
        targets.push_back(L"gamemd-spawn.exe");
        targets.push_back(L"gamemd.exe");
    }

    // Фаза 1: поллинг процессов каждые 500мс до 120с.
    constexpr DWORD kWaitMs = 120000;
    constexpr DWORD kPollMs = 500;
    DWORD pid = 0;
    std::wstring foundName;
    DWORD startTick = GetTickCount64();
    while (GetTickCount64() - startTick < kWaitMs) {
        for (const auto& t : targets) {
            DWORD p = FindProcessByName(t.c_str());
            if (p != 0) { pid = p; foundName = t; break; }
        }
        if (pid) break;
        Sleep(kPollMs);
    }
    if (pid == 0) {
        LogLine(L"Attach: no target process found within 120s, giving up");
        return 1;
    }
    LogLine(L"Attach: found process '" + foundName + L"' (PID " + std::to_wstring(pid) + L")");

    // Фаза 2: ждём Ares.dll + Phobos.dll + CnCNet-Spawner.dll (до 15с) - это
    // гарантирует, что Syringe уже внедрил игровые расширения до нашего хука.
    static const wchar_t* kWaitDlls[] = { L"Ares.dll", L"Phobos.dll", L"CnCNet-Spawner.dll" };
    constexpr DWORD kModuleWaitMs = 15000;
    bool allPresent = false;
    DWORD modStart = GetTickCount64();
    while (GetTickCount64() - modStart < kModuleWaitMs) {
        auto mods = GetProcessModules(pid);
        allPresent = true;
        for (const wchar_t* dll : kWaitDlls) {
            bool found = false;
            for (const auto& m : mods) {
                if (_wcsicmp(m.c_str(), dll) == 0) { found = true; break; }
            }
            if (!found) { allPresent = false; break; }
        }
        if (allPresent) break;
        Sleep(500);
    }
    if (allPresent) {
        LogLine(L"Attach: Ares.dll + Phobos.dll + CnCNet-Spawner.dll present (Syringe injected)");
    } else {
        LogLine(L"Attach: WARN - expected mods not all present within 15s, proceeding anyway");
    }

    // Пауза 1с для стабилизации после Syringe-инъекций.
    Sleep(1000);

    // База модуля игры.
    uintptr_t modBase = GetModuleBase(pid, foundName.c_str());
    LogLine(L"Attach: module base = " + HexWord(modBase) + L" (module '" + foundName + L"')");

    // Живые байты (16) ДО инъекта — сравнить ваниль vs CnCNet.
    uint8_t live[2][16];
    bool okA = ReadLiveBytes(pid, kSigAddrMainLoop, live[0], 16);
    bool okB = ReadLiveBytes(pid, kSigAddrLoadString, live[1], 16);
    if (okA) {
        bool match = (memcmp(live[0], kSigMainLoop, 8) == 0);
        LogLine(L"Attach: live bytes @0x0055D360 = " + BytesToHexStr(live[0], 16) +
                (match ? L"  [MATCH vanilla]" : L"  [MISMATCH!]"));
    } else {
        LogLine(L"Attach: could not read @0x0055D360");
    }
    if (okB) {
        bool match = (memcmp(live[1], kSigLoadString, 8) == 0);
        LogLine(L"Attach: live bytes @0x00734E60 = " + BytesToHexStr(live[1], 16) +
                (match ? L"  [MATCH vanilla]" : L"  [MISMATCH!]"));
    } else {
        LogLine(L"Attach: could not read @0x00734E60");
    }

    // Инъект LuaAPI.dll.
    std::wstring error;
    if (!InjectDllIntoProcess(pid, dllPath, &error)) {
        LogLine(L"Attach: injection FAILED: " + error);
        return 1;
    }

    g_gamePid = pid;
    g_gameName = foundName;
    LogLine(L"Attach: LuaAPI.dll injected into PID " + std::to_wstring(pid));

    // Ждём выхода игры (запущена внешним клиентом), чтобы зафиксировать код выхода.
    HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        WaitForSingleObject(hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(hProcess, &code);
        wchar_t b[16];
        swprintf(b, 16, L"%08X", code);
        LogLine(L"Attach: '" + foundName + L"' exited code=0x" + std::wstring(b));
        CloseHandle(hProcess);
    } else {
        LogLine(L"Attach: OpenProcess failed, cannot wait for exit (error " +
                std::to_wstring(GetLastError()) + L")");
    }
    Sleep(500);
    return 0;
}

// ---------------------------------------------------------------------------
// Mods
// ---------------------------------------------------------------------------

std::vector<std::wstring> LoadActiveModIds(const std::wstring& exeDir) {
    std::vector<std::wstring> ids;
    std::ifstream file(exeDir + L"\\scripts\\active_mods.txt");
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first) {
            first = false;
            if (line.size() >= 3 && (unsigned char)line[0]==0xEF && (unsigned char)line[1]==0xBB && (unsigned char)line[2]==0xBF)
                line.erase(0,3);
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        if (line[start] == '#')
            continue;
        // trim end already done, extract id
        std::string id = line.substr(start);
        // trim trailing spaces inside id
        size_t end = id.find_last_not_of(" \t");
        if (end != std::string::npos) id = id.substr(0, end+1);
        ids.push_back(std::wstring(id.begin(), id.end()));
    }
    return ids;
}

std::wstring JsonGetString(const std::wstring& json, const wchar_t* key) {
    std::wstring pattern = std::wstring(L"\"") + key + L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos)
        return L"";
    size_t colon = json.find(L':', keyPos + pattern.size());
    size_t openQuote = json.find(L'"', colon);
    size_t closeQuote = json.find(L'"', openQuote + 1);
    if (colon == std::wstring::npos || openQuote == std::wstring::npos || closeQuote == std::wstring::npos)
        return L"";
    return json.substr(openQuote + 1, closeQuote - openQuote - 1);
}

std::vector<std::wstring> JsonGetStringArray(const std::wstring& json, const wchar_t* key) {
    std::vector<std::wstring> out;
    std::wstring pattern = std::wstring(L"\"") + key + L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos)
        return out;
    size_t openBracket = json.find(L'[', keyPos);
    size_t closeBracket = json.find(L']', openBracket == std::wstring::npos ? 0 : openBracket);
    if (openBracket == std::wstring::npos || closeBracket == std::wstring::npos || closeBracket <= openBracket)
        return out;

    std::wstring body = json.substr(openBracket + 1, closeBracket - openBracket - 1);
    size_t pos = 0;
    while ((pos = body.find(L'"', pos)) != std::wstring::npos) {
        size_t end = body.find(L'"', pos + 1);
        if (end == std::wstring::npos)
            break;
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
    if (find == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        ModEntry entry{};
        entry.dir = fd.cFileName;
        entry.id = fd.cFileName;

        std::wstring manifest = exeDir + L"\\scripts\\mods\\" + entry.dir + L"\\mod.json";
        if (FileExists(manifest)) {
            std::ifstream f(manifest);
            std::stringstream ss;
            ss << f.rdbuf();
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
                if (entry.id.empty())
                    entry.id = entry.dir;
                if (entry.name.empty())
                    entry.name = entry.id;
            }
        } else if (!FileExists(exeDir + L"\\scripts\\mods\\" + entry.dir + L"\\main.lua")) {
            continue;
        }

        if (entry.name.empty())
            entry.name = entry.id;
        if (entry.author.empty())
            entry.author = L"unknown";

        for (const auto& id : activeIds) {
            if (_wcsicmp(entry.id.c_str(), id.c_str()) == 0) {
                entry.enabled = true;
                break;
            }
        }

        g_mods.push_back(entry);
    } while (FindNextFileW(find, &fd));

    FindClose(find);

    // Уважаем пользовательский порядок, сохранённый в active_mods.txt: порядок строк файла
    // = порядок включённых модов после реордера. Моды из файла идут первыми — в их порядке,
    // прочие (новые / выключенные) — после, в файловом (алфавитном) порядке, как раньше.
    if (!activeIds.empty()) {
        std::vector<ModEntry> ordered;
        std::vector<bool> used(g_mods.size(), false);
        for (const auto& id : activeIds) {
            for (size_t i = 0; i < g_mods.size(); ++i) {
                if (!used[i] && _wcsicmp(g_mods[i].id.c_str(), id.c_str()) == 0) {
                    ordered.push_back(std::move(g_mods[i]));
                    used[i] = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < g_mods.size(); ++i) {
            if (!used[i]) ordered.push_back(std::move(g_mods[i]));
        }
        g_mods = std::move(ordered);
    }
    RebuildVisible();
}

void SaveMods() {
    std::wstring exeDir = GetExeDirectory();
    std::wofstream out(exeDir + L"\\scripts\\active_mods.txt", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        g_appState = AppState::StatusError;
        SetStatusKey(StatusKey::SaveFail);
        MessageBoxW(g_hwnd, L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0437\u0430\u043F\u0438\u0441\u0430\u0442\u044C active_mods.txt",
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return;
    }

    out << L"# LuaAPI active mods - one mod ID per line\n";
    int saved = 0;
    for (const auto& m : g_mods) {
        if (m.enabled) {
            out << m.id << L"\n";
            ++saved;
        }
    }
    out.flush();
    out.close();
    g_dirty = false;
    g_appState = AppState::Ready;
    ShowToast(L10N(L"\u2713 \u0421\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u043E", L"\u2713 Saved"));
    // toast timer will revert to Ready after 2s, just invalidate now
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

int EnabledModCount() {
    int n = 0;
    for (const auto& m : g_mods)
        if (m.enabled) ++n;
    return n;
}

// Поиск без учёта регистра (строчные копии). Используется для имени/автора/описания.
bool MatchesFilter(const ModEntry& m, const std::wstring& q) {
    if (q.empty()) return true;
    std::wstring needle = q;
    for (auto& c : needle) c = towlower(c);
    auto contains = [&](const std::wstring& s) -> bool {
        std::wstring t = s;
        for (auto& c : t) c = towlower(c);
        return t.find(needle) != std::wstring::npos;
    };
    return contains(m.name) || contains(m.author) || contains(m.description) || contains(m.id);
}

// Пересчёт видимого (отфильтрованного) подмножества модов.
void RebuildVisible() {
    g_visible.clear();
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (MatchesFilter(g_mods[i], g_searchQuery))
            g_visible.push_back(static_cast<int>(i));
    }
    // При изменении фильтра показываем список с начала.
    g_scroll = 0;
}

std::vector<std::pair<size_t, size_t>> DetectConflicts() {
    std::vector<std::pair<size_t, size_t>> hits;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!g_mods[i].enabled) continue;
        for (size_t j = i + 1; j < g_mods.size(); ++j) {
            if (!g_mods[j].enabled) continue;
            bool conflict = false;
            for (const auto& c : g_mods[i].conflicts) if (_wcsicmp(c.c_str(), g_mods[j].id.c_str())==0) { conflict = true; break; }
            if (!conflict) for (const auto& c : g_mods[j].conflicts) if (_wcsicmp(c.c_str(), g_mods[i].id.c_str())==0) { conflict = true; break; }
            if (conflict) hits.emplace_back(i, j);
        }
    }
    return hits;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------


COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(GetRValue(a) + static_cast<int>((GetRValue(b) - GetRValue(a)) * t),
               GetGValue(a) + static_cast<int>((GetGValue(b) - GetGValue(a)) * t),
               GetBValue(a) + static_cast<int>((GetBValue(b) - GetBValue(a)) * t));
}

void PaintAll(HDC dc) {
    RECT full{ 0, 0, g_clientW, g_clientH };
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &full, bg);
    DeleteObject(bg);

    int w = g_clientW;
    // Контентная область: до 1200px — растягивается, шире — центрируется (то же, что в ComputeLayout).
    int effW = std::min(w, kMaxContentWidth);
    int xOffset = (w - effW) / 2;

    // ---- Header: заголовок + подзаголовок слева, RU/EN справа (одна базовая линия) ----
    DrawTextR(dc, L"RED ALERT 2 - LUA ENGINE",
              RECT{xOffset + kPad, 14, xOffset + effW - 150, 44}, g_fontTitle, kText);
    DrawTextR(dc, Str_Subtitle(), RECT{xOffset + kPad, 44, xOffset + effW - kPad, 62}, g_fontSmall, kDim);

    // Сегментированный переключатель языка RU/EN (активный сегмент — акцентная подсветка).
    {
        RECT r = g_rcLang;
        FillRoundRect(dc, r, g_hoverLang ? kSurfaceHov : kSurface, Tok::RadiusPill);
        int half = (r.right - r.left) / 2;
        RECT ru{ r.left, r.top, r.left + half + 2, r.bottom };
        RECT en{ r.left + half - 2, r.top, r.right, r.bottom };
        RECT act = g_isRussian ? ru : en;
        FillRoundRect(dc, act, kSurfaceHov, Tok::RadiusPill - 2);
        {
            HPEN pen = CreatePen(PS_SOLID, 2, Tok::Accent);
            auto oldPen = SelectObject(dc, pen);
            int ax = (act.left + act.right) / 2 - 10;
            MoveToEx(dc, ax, act.bottom - 6, nullptr);
            LineTo(dc, ax + 20, act.bottom - 6);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
        DrawTextR(dc, L"RU", ru, g_fontSmall, g_isRussian ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, L"EN", en, g_fontSmall, g_isRussian ? kDim : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ---- Status row ----
    {
        int dotX = xOffset + kPad + 6;
        int cy = 84;
        DrawCircle(dc, dotX, cy, 5, CurrentStatusColor());
        DrawTextR(dc, CurrentStatusText(), RECT{xOffset + kPad + 18, cy - 12, xOffset + effW - kPad, cy + 12}, g_fontBody, kText);
    }

    // ---- Action buttons (Launch / Inject): одинаковая высота, radius 6, hover + pressed ----
    {
        bool launchEnabled = FileExists(GetExeDirectory() + L"\\gamemd.exe");
        bool launchDown = g_down && g_hoverLaunch;
        COLORREF launchFill = !launchEnabled ? Tok::Disabled
                            : (launchDown ? LerpColor(kRed, kBg, 0.22f)
                            : (g_hoverLaunch ? Tok::LaunchHov : Tok::Launch));
        FillRoundRect(dc, g_rcLaunch, launchFill, Tok::RadiusBtn);
        DrawTextR(dc, Str_LaunchBtn(), g_rcLaunch, g_fontCard,
                  !launchEnabled ? kDim : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        bool injectDown = g_down && g_hoverInject;
        COLORREF injectFill = g_injecting ? Tok::Disabled
                            : (injectDown ? LerpColor(kBlue, kBg, 0.22f)
                            : (g_hoverInject ? Tok::InjectHov : Tok::Inject));
        FillRoundRect(dc, g_rcInject, injectFill, Tok::RadiusBtn);
        DrawTextR(dc, Str_InjectBtn(), g_rcInject, g_fontCard, g_injecting ? kDim : kText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ---- Section label + счётчик модов (слева) и поиск (справа) ----
    {
        int secTop = g_rcLaunch.bottom + 16;
        int secBot = g_rcLaunch.bottom + 36;
        std::wstring header = std::wstring(Str_ModsHeader()) + L" (" + std::to_wstring(static_cast<int>(g_mods.size())) + L")";
        if (!g_searchQuery.empty())
            header += L"  \u00B7  " + std::to_wstring(static_cast<int>(g_visible.size())) + L" " +
                      L10N(L"\u043F\u043E\u043A\u0430\u0437\u0430\u043D\u043E", L"shown");
        DrawTextR(dc, header, RECT{xOffset + kPad, secTop, g_rcSearch.left - Tok::S12, secBot},
                  g_fontSmall, kDim);

        // Поле поиска (правая часть строки заголовка).
        RECT s = g_rcSearch;
        bool sHover = g_hoverSearch;
        FillRoundRect(dc, s, sHover ? kHover : kSurface, Tok::RadiusBtn,
                      g_searchFocused ? Tok::Accent : Tok::Border, true);
        if (!g_searchQuery.empty()) {
            // Текст запроса.
            DrawTextR(dc, g_searchQuery, RECT{s.left + Tok::S12, s.top, s.right - 24, s.bottom},
                      g_fontSmall, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            // Кнопка-очистка "×".
            RECT clear{ s.right - 20, s.top, s.right - 4, s.bottom };
            DrawTextR(dc, L"\u00D7", clear, g_fontSmall, sHover ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            DrawTextR(dc, L10N(L"\u041F\u043E\u0438\u0441\u043A \u043C\u043E\u0434\u043E\u0432\u2026", L"Search mods\u2026"),
                      RECT{s.left + Tok::S12, s.top, s.right - 12, s.bottom},
                      g_fontSmall, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // ---- Mod cards ---- (СТРОГО внутри маски списка)
    Layout l = ComputeLayout(g_clientW, g_clientH);
    int savedDC = SaveDC(dc);
    IntersectClipRect(dc, l.list.left, l.list.top, l.list.right, l.list.bottom);

    // Empty state UX (внутри маски)
    if (g_mods.empty()) {
        DrawTextR(dc, L10N(L"\u041C\u043E\u0434\u044B \u043D\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043B\u0435\u043D\u044B", L"No mods installed"),
                  RECT{xOffset + kPad, l.list.top + 20, xOffset + effW - kPad, l.list.top + 44}, g_fontCard, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextR(dc, L10N(L"\u041F\u043E\u043C\u0435\u0441\u0442\u0438\u0442\u0435 \u043F\u0430\u043F\u043A\u0438 \u0432 scripts/mods/ \u0438 \u043F\u0435\u0440\u0435\u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435",
                           L"Place folders in scripts/mods/ and restart"),
                  RECT{xOffset + kPad, l.list.top + 48, xOffset + effW - kPad, l.list.top + 70}, g_fontBody, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (g_visible.empty()) {
        DrawTextR(dc, L10N(L"\u041D\u0438\u0447\u0435\u0433\u043E \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D\u043E \u043F\u043E \u0437\u0430\u043F\u0440\u043E\u0441\u0443",
                           L"No mods match your search"),
                  RECT{xOffset + kPad, l.list.top + 24, xOffset + effW - kPad, l.list.top + 48}, g_fontBody, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Отрисовка карточек модов с учётом скролла и фильтра (шаг = kCardH + kCardGap).
    int listH = l.list.bottom - l.list.top;
    int totalH = static_cast<int>(g_visible.size()) * kCardStep;
    int cardW = (l.list.right - l.list.left) - (totalH > listH ? (kScrollW + 8) : 0);
    int yPos = l.list.top + 4 - g_scroll;
    for (size_t i = 0; i < g_visible.size(); ++i) {
        const ModEntry& m = g_mods[g_visible[i]];
        int idx = static_cast<int>(i);   // видимый индекс (позиция в списке)
        if (yPos + kCardH >= l.list.top && yPos <= l.list.bottom) {
            RECT rcCard = { l.list.left, yPos, l.list.left + cardW, yPos + kCardH };
            bool isDrag = g_dragState.dragging && g_dragState.dragIndex == idx;

            // Hover-подсветка карточки с плавным переходом (~120 мс).
            // Рамка включённой карточки смягчена до нейтральной, а состояние отмечено
            // тонкой акцентной полосой слева (чтобы не спорила с hover-подсветкой).
            float hf = (idx == g_hoverCardIdx) ? g_hoverFade : 0.f;
            COLORREF cardFill = isDrag ? kBlue : LerpColor(kSurface, kHover, hf);
            FillRoundRect(dc, rcCard, cardFill, Tok::RadiusCard, Tok::Border, true);
            if (m.enabled && !isDrag) {
                RECT bar{ rcCard.left + 1, rcCard.top + 10, rcCard.left + 4, rcCard.bottom - 10 };
                FillRoundRect(dc, bar, kGreen, 2);
            }

            // Всё в едином «хэдэр-строке» карточки: чекбокс, имя, автор, бейдж, иконки.
            int rowTop = yPos + 12;
            int rowBot = yPos + 36;
            int rowCy  = (rowTop + rowBot) / 2;   // y+24

            RECT box{ rcCard.left + kCardInner, rowCy - 9, rcCard.left + kCardInner + 18, rowCy + 9 };
            DrawCheckbox(dc, box, m.enabled);
            int tx = rcCard.left + kCardInner + 18 + 12;   // контент после чекбокса

            // Правый блок, вся строка: [имя ▲][author][бейдж][иконки], выровнен по rowCy.
            RECT rowRect{ rcCard.left, rowTop, rcCard.right, rowBot };
            RECT folderBtn, pencilBtn;
            CardActionButtons(rowRect, &folderBtn, &pencilBtn);

            std::wstring badge = L"v" + m.version;
            HFONT mfont = static_cast<HFONT>(SelectObject(dc, g_fontSmall));
            SIZE bsz{};
            GetTextExtentPoint32W(dc, badge.c_str(), static_cast<int>(badge.size()), &bsz);
            SelectObject(dc, mfont);
            int badgeRight = folderBtn.left - Tok::S8;
            RECT badgeRc{ badgeRight - bsz.cx - 16, rowCy - 10, badgeRight, rowCy + 10 };
            int authorRight = badgeRc.left - Tok::S8;
            int authorMax = 120;
            int nameRight = std::max(tx + 80, authorRight - authorMax - Tok::S8);

            DrawTextR(dc, m.name, RECT{tx, rowTop, nameRight, rowBot}, g_fontCard, kText);
            FillRoundRect(dc, badgeRc, kBadge, 5);
            DrawTextR(dc, badge, badgeRc, g_fontSmall, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextR(dc, L"by " + m.author, RECT{authorRight - authorMax, rowCy - 10, authorRight, rowCy + 10},
                      g_fontSmall, kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

            // Описание (12pt, вторичный цвет) — полная строка под хэдэр-строкой.
            DrawTextR(dc, m.description, RECT{tx, yPos + 42, rcCard.right - kCardInner, yPos + 66},
                      g_fontBody, kDim, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Кнопки быстрого доступа (папка / карандаш) — на той же строке, справа.
            {
                bool folderHov = (g_hoverActionCard == idx && g_hoverActionBtn == 1);
                bool pencilHov = (g_hoverActionCard == idx && g_hoverActionBtn == 2);
                FillRoundRect(dc, folderBtn, folderHov ? kHover : kSurface, Tok::RadiusBtn, Tok::Border, true);
                DrawFolderIcon(dc, folderBtn, folderHov ? kText : kDim);
                FillRoundRect(dc, pencilBtn, pencilHov ? kHover : kSurface, Tok::RadiusBtn, Tok::Border, true);
                DrawPencilIcon(dc, pencilBtn, pencilHov ? kText : kDim);
            }
        }
        yPos += kCardStep;
    }

    // Восстанавливаем контекст (снимаем маску отсечения)
    RestoreDC(dc, savedDC);

// ---- Conflict banner ----
    auto conflicts = DetectConflicts();
    if (!conflicts.empty()) {
        std::wstring warning;
        for (size_t k = 0; k < conflicts.size(); ++k) {
            warning += L"! " + g_mods[conflicts[k].first].name +
                       L" vs " + g_mods[conflicts[k].second].name;
            if (k + 1 < conflicts.size())
                warning += L";  ";
        }
        Layout bl = ComputeLayout(g_clientW, g_clientH);
        int bannerTop = bl.bannerTop, bannerBottom = bl.bannerBottom;
        // Не заходить ниже футер-планки (разделитель на footerTop).
        if (bannerBottom > bl.footerTop) { bannerBottom = bl.footerTop; bannerTop = bannerBottom - 24; }
        if (bannerTop < bl.list.top) { bannerTop = bl.list.top; bannerBottom = bannerTop + 24; }
        DrawTextR(dc, warning, RECT{xOffset + kPad, bannerTop, xOffset + effW - kPad, bannerBottom}, g_fontSmall, kOrange);
    }

    // ---- Footer: единая планка (Active X of Y слева, Save Apply справа, разделитель сверху) ----
    {
        Layout fl = ComputeLayout(g_clientW, g_clientH);
        // Тонкий разделитель над футер-планкой.
        {
            HPEN pen = CreatePen(PS_SOLID, 1, Tok::Divider);
            auto oldPen = SelectObject(dc, pen);
            MoveToEx(dc, xOffset + kPad, fl.footerTop, nullptr);
            LineTo(dc, xOffset + effW - kPad, fl.footerTop);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
        // Active X of Y — слева, по вертикальному центру планки.
        DrawTextR(dc, Str_ActiveCount(EnabledModCount(), static_cast<int>(g_mods.size())),
                  RECT{xOffset + kPad, fl.footerTop, xOffset + kPad + 320, fl.footerBottom},
                  g_fontBody, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // Save & Apply — справа (g_rcSave выровнен по планке из ComputeLayout).
        bool saveDown = g_down && g_hoverSave;
        COLORREF saveFill = g_launching ? Tok::Disabled
                          : (saveDown ? LerpColor(kGreen, kBg, 0.22f)
                          : (g_hoverSave ? kGreenHover : kGreen));
        FillRoundRect(dc, fl.save, saveFill, Tok::RadiusBtn);
        DrawTextR(dc, Str_SaveBtn(), fl.save, g_fontCard, g_launching ? kDim : kText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ---- Тонкий скроллбар в тему (kScrollW=6, в диапазоне l.list.top..bottom)
    {
        Layout l = ComputeLayout(g_clientW, g_clientH);
        int listHeight = l.list.bottom - l.list.top;
        int totalModHeight = static_cast<int>(g_visible.size()) * kCardStep;
        if (totalModHeight > listHeight) {
            int maxScroll = totalModHeight - listHeight;
            int trackH = listHeight - 8;
            int trackX = l.list.right - kScrollW;
            int trackY = l.list.top + 4;
            FillRoundRect(dc, RECT{trackX, trackY, trackX + kScrollW, trackY + trackH}, Tok::ScrollTrack, 3);
            int thumbH = std::max(20, trackH * listHeight / totalModHeight);
            int thumbY = trackY + (maxScroll ? (g_scroll * (trackH - thumbH) / maxScroll) : 0);
            FillRoundRect(dc, RECT{trackX, thumbY, trackX + kScrollW, thumbY + thumbH}, Tok::ScrollThumb, 3);
        }
    }

    // ---- Тултип для кнопок быстрого доступа (папка / карандаш)
    if (g_tooltipVisible) {
        RECT t = g_tooltipAnchor;
        SIZE sz{};
        HFONT oldf = static_cast<HFONT>(SelectObject(dc, g_fontSmall));
        GetTextExtentPoint32W(dc, g_tooltipText.c_str(), static_cast<int>(g_tooltipText.size()), &sz);
        SelectObject(dc, oldf);
        int padX = Tok::S12, padY = Tok::S8;
        int w = sz.cx + padX * 2;
        int h = sz.cy + padY * 2;
        int lx = (t.left + t.right) / 2 - w / 2;
        int ly = t.top - h - 6;
        if (ly < 0) ly = t.bottom + 6;   // сверху нет места — показываем под кнопкой
        RECT box{ lx, ly, lx + w, ly + h };
        FillRoundRect(dc, box, Tok::Surface, 4, Tok::Border, true);
        DrawTextR(dc, g_tooltipText, box, g_fontSmall, Tok::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------------------
// Hit testing / interaction
// ---------------------------------------------------------------------------

bool PointIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

// Возвращает ВИДИМЫЙ индекс карточки под курсором (с учётом скролла и фильтра) или -1.
int CardIndexAt(POINT pt) {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    if (pt.y < l.list.top || pt.y > l.list.bottom) return -1;
    int listH = l.list.bottom - l.list.top;
    int totalH = static_cast<int>(g_visible.size()) * kCardStep;
    int cardW = (l.list.right - l.list.left) - (totalH > listH ? (kScrollW + 8) : 0);
    int yPos = l.list.top + 4 - g_scroll;
    for (size_t i = 0; i < g_visible.size(); ++i) {
        RECT rcCard = { l.list.left, yPos, l.list.left + cardW, yPos + kCardH };
        if (PointIn(rcCard, pt)) return static_cast<int>(i);
        yPos += kCardStep;
    }
    return -1;
}

// Хит-тест кнопок быстрого доступа на карточке: 1 = папка, 2 = карандаш, 0 = нет.
// Возвращает ВИДИМЫЙ индекс мода через *outIdx. Проверяет ТОЛЬКО две маленькие кнопки,
// поэтому клик по ним не попадает в общую логику click/drag карточки.
int CardActionAt(POINT pt, int* outIdx) {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    int listH = l.list.bottom - l.list.top;
    int totalH = static_cast<int>(g_visible.size()) * kCardStep;
    int cardW = (l.list.right - l.list.left) - (totalH > listH ? (kScrollW + 8) : 0);
    int yPos = l.list.top + 4 - g_scroll;
    for (size_t i = 0; i < g_visible.size(); ++i) {
        RECT rcCard = { l.list.left, yPos, l.list.left + cardW, yPos + kCardH };
        // Кнопки выровнены по «хэдэр-строке» карточки (ty+12..ty+36) — та же зона, что в PaintAll.
        RECT rowRc{ rcCard.left, yPos + 12, rcCard.right, yPos + 36 };
        RECT folder, pencil;
        CardActionButtons(rowRc, &folder, &pencil);
        if (PointIn(folder, pt)) { if (outIdx) *outIdx = static_cast<int>(i); return 1; }
        if (PointIn(pencil, pt)) { if (outIdx) *outIdx = static_cast<int>(i); return 2; }
        yPos += kCardStep;
    }
    if (outIdx) *outIdx = -1;
    return 0;
}

POINT CursorInClient() {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(g_hwnd, &p);
    return p;
}

void OnLeftDown(POINT pt) {
    if (PointIn(g_rcLang, pt)) {
        g_isRussian = !g_isRussian;
        SavePrefs();
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }

    // Клик вне поля поиска снимает с него фокус ввода.
    if (!PointIn(g_rcSearch, pt))
        g_searchFocused = false;

    // Поле поиска: захват фокуса ввода (или очистка по клику на «×»).
    if (PointIn(g_rcSearch, pt)) {
        if (!g_searchQuery.empty() && pt.x >= g_rcSearch.right - 24) {
            // клик по «×» — очистить запрос
            g_searchQuery.clear();
            RebuildVisible();
            ClampScroll();
        }
        g_searchFocused = true;
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }

    // Кнопки быстрого доступа на карточке мода: открыть папку или main.lua.
    // Возвращаемся сразу — НЕ включаем pendingClick/drag, поэтому WM_LBUTTONUP
    // не тронет чекбокс и не начнёт перетаскивание.
    {
        int actIdx = -1;   // видимый индекс
        int act = CardActionAt(pt, &actIdx);
        if (act != 0 && actIdx >= 0 && actIdx < static_cast<int>(g_visible.size())) {
            const ModEntry& m = g_mods[g_visible[actIdx]];   // реальный индекс через фильтр
            std::wstring modDir = GetExeDirectory() + L"\\scripts\\mods\\" + m.dir;
            if (act == 1) {
                ShellExecuteW(g_hwnd, L"explore", modDir.c_str(), nullptr, nullptr, SW_SHOW);
            } else {
                ShellExecuteW(g_hwnd, L"open", (modDir + L"\\main.lua").c_str(), nullptr, nullptr, SW_SHOW);
            }
            return;
        }
    }
    if (g_launching || g_injecting) return; // disable clicks while busy
    if (PointIn(g_rcLaunch, pt)) { DoLaunchGame(); return; }
    if (PointIn(g_rcInject, pt)) { DoInjectAttach(); return; }
    if (PointIn(g_rcSave, pt)) { SaveMods(); return; }

    // Клик по карточке: держим захват мыши, чтобы отличить обычный клик (тоггл чекбокса)
    // от перетаскивания (сдвиг > 6px) в WM_MOUSEMOVE. При активном фильтре drag отключён.
    int idx = CardIndexAt(pt);
    if (idx >= 0) {
        g_dragState.pendingClick = true;
        g_dragState.pendingIndex = idx;
        g_dragState.downPos = pt;
        g_dragState.dragging = false;
        g_dragState.dragIndex = -1;
        if (g_searchQuery.empty())
            SetCapture(g_hwnd);
    }
}

} // namespace


void RecalcLayout() {
    Layout l = ComputeLayout(g_clientW, g_clientH);
    g_rcLaunch = l.launch;
    g_rcInject = l.inject;
    g_rcLang = l.lang;
    g_rcSearch = l.search;
    g_rcSave = l.save;
    // keep clamped
    ClampScroll();
}

// Переключение полноэкранного режима (borderless). Сохраняет окно-геометрию при входе
// и восстанавливает её при выходе. Клиентская область обновляется через WM_SIZE.
void ToggleFullscreen() {
    if (!g_hwnd) return;

    if (!g_fullscreen) {
        GetWindowRect(g_hwnd, &g_windowedRect);
        // Borderless: только WS_POPUP, без WS_EX_TOPMOST (он накрывал бы окна других
        // приложений, даже без фокуса). Разворачиваемся в рабочую область монитора под окном
        // (rcWork — без таскбара), а не в весь экран, чтобы таскбар остался видимой.
        LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
        SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);

        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            const RECT& w = mi.rcWork;
            SetWindowPos(g_hwnd, HWND_TOP, w.left, w.top,
                         w.right - w.left, w.bottom - w.top,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            // Fallback на весь экран, если не удалось получить информацию о мониторе.
            SetWindowPos(g_hwnd, HWND_TOP, 0, 0,
                         GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        g_fullscreen = true;
    } else {
        LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
        SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) |
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
        SetWindowPos(g_hwnd, HWND_TOP,
                     g_windowedRect.left, g_windowedRect.top,
                     g_windowedRect.right - g_windowedRect.left,
                     g_windowedRect.bottom - g_windowedRect.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
        g_fullscreen = false;
    }
    RecalcLayout();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
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
        SetStatusKey(StatusKey::Ready);
        return 0;
    }
    case WM_SIZE:
        g_clientW = LOWORD(lParam);
        g_clientH = HIWORD(lParam);
        RecalcLayout();
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            bool overCard = CardIndexAt(pt) >= 0;
            bool overBtn = PointIn(g_rcLaunch, pt) || PointIn(g_rcInject, pt) ||
                           PointIn(g_rcSave, pt) || PointIn(g_rcLang, pt) || PointIn(g_rcSearch, pt);
            // При активном фильтре над карточкой — курсор «запрещено» (drag недоступен).
            if (!g_searchQuery.empty() && overCard) {
                SetCursor(LoadCursor(nullptr, IDC_NO));
                return TRUE;
            }
            if (overCard || overBtn) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CHAR:
        // Ввод в поле поиска; BM_CHAR приходит когда окно в фокусе.
        if (g_searchFocused) {
            wchar_t c = static_cast<wchar_t>(wParam);
            if (c == 8) {                       // backspace
                if (!g_searchQuery.empty()) {
                    g_searchQuery.pop_back();
                    RebuildVisible();
                    ClampScroll();
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            } else if (c >= 0x20 && c != 0x7F) { // печатный символ
                g_searchQuery.push_back(c);
                RebuildVisible();
                ClampScroll();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_F11) {
            ToggleFullscreen();
            return 0;
        }
        if (g_searchFocused && wParam == VK_ESCAPE) {
            g_searchQuery.clear();
            g_searchFocused = false;
            RebuildVisible();
            ClampScroll();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        if (wParam == VK_ESCAPE && g_fullscreen) {
            ToggleFullscreen();
            return 0;
        }
        break; // не обработанные клавиши — в DefWindowProc, а не молча глотать
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        // ---- Drag-and-drop reorder of mod cards ----
        if (g_dragState.pendingClick || g_dragState.dragging) {
            if (g_dragState.pendingClick) {
                long adx = pt.x - g_dragState.downPos.x; adx = adx < 0 ? -adx : adx;
                long ady = pt.y - g_dragState.downPos.y; ady = ady < 0 ? -ady : ady;
                // Reorder-перетаскивание доступно только когда фильтр поиска не активен.
                if ((adx > 6 || ady > 6) && g_searchQuery.empty()) {
                    g_dragState.dragging = true;
                    g_dragState.pendingClick = false;
                    g_dragState.dragIndex = g_dragState.pendingIndex;
                    g_dragState.dragAnchorY = pt.y;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            if (g_dragState.dragging) {
                int dy = pt.y - g_dragState.dragAnchorY;
                if (dy >= kCardStep / 2 && g_dragState.dragIndex + 1 < static_cast<int>(g_mods.size())) {
                    std::swap(g_mods[g_dragState.dragIndex], g_mods[g_dragState.dragIndex + 1]);
                    g_dragState.dragIndex += 1;
                    g_dragState.dragAnchorY += kCardStep;
                    InvalidateRect(hwnd, nullptr, TRUE);
                } else if (dy <= -kCardStep / 2 && g_dragState.dragIndex - 1 >= 0) {
                    std::swap(g_mods[g_dragState.dragIndex], g_mods[g_dragState.dragIndex - 1]);
                    g_dragState.dragIndex -= 1;
                    g_dragState.dragAnchorY -= kCardStep;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }
        bool hL = (!g_launching && !g_injecting) && PointIn(g_rcLaunch, pt);
        bool hI = (!g_launching && !g_injecting) && PointIn(g_rcInject, pt);
        bool hS = (!g_launching && !g_injecting) && PointIn(g_rcSave, pt);
        bool hG = PointIn(g_rcLang, pt);
        bool overList = false;
        // Only invalidate overList if it changes hover state of cards - throttle
        if ((hL != g_hoverLaunch) || (hI != g_hoverInject) ||
            (hS != g_hoverSave) || (hG != g_hoverLang)) {
            g_hoverLaunch = hL;
            g_hoverInject = hI;
            g_hoverSave = hS;
            g_hoverLang = hG;
            InvalidateRect(hwnd, nullptr, TRUE);
        } else {
            // check card hover without invalidating every move: only if card under cursor changed
            static int lastHoverIdx = -1;
            int idx = -1;
            Layout hl = ComputeLayout(g_clientW, g_clientH);
            int hListH = hl.list.bottom - hl.list.top;
            int hTotalH = (int)g_visible.size() * kCardStep;
            int hCardW = (hl.list.right - hl.list.left) - (hTotalH > hListH ? (kScrollW + 8) : 0);
            int yPos2 = hl.list.top + 4 - g_scroll;
            for (size_t i=0;i<g_visible.size();++i){ RECT card{ hl.list.left, yPos2, hl.list.left + hCardW, yPos2 + kCardH }; if (PointIn(card, pt)) { idx=(int)i; break; } yPos2+=kCardStep; }
            if (idx != lastHoverIdx) { lastHoverIdx = idx; InvalidateRect(hwnd, nullptr, TRUE); }
        }
        // Hover-состояние кнопок быстрого доступа (папка/карандаш) + тултип.
        {
            int actIdx = -1;
            int act = CardActionAt(pt, &actIdx);
            if (act != g_hoverActionBtn || actIdx != g_hoverActionCard) {
                g_hoverActionBtn = act;
                g_hoverActionCard = actIdx;
                InvalidateRect(hwnd, nullptr, TRUE);
                // Пересчёт якоря и текста тултипа под наведённой кнопкой.
                g_tooltipVisible = false;
                if (act != 0 && actIdx >= 0 && actIdx < static_cast<int>(g_mods.size())) {
                    Layout tl = ComputeLayout(g_clientW, g_clientH);
                    int tH = static_cast<int>(g_visible.size()) * kCardStep;
                    int tListH = tl.list.bottom - tl.list.top;
                    int tW = (tl.list.right - tl.list.left) - (tH > tListH ? (kScrollW + 8) : 0);
                    int ty = tl.list.top + 4 - g_scroll + actIdx * kCardStep;
                    RECT cc{ tl.list.left, ty + 12, tl.list.left + tW, ty + 36 };   // хэдэр-строка (та же, что в отрисовке)
                    RECT fb, pb;
                    CardActionButtons(cc, &fb, &pb);
                    g_tooltipAnchor = (act == 1) ? fb : pb;
                    g_tooltipText = (act == 1)
                        ? L10N(L"\u041E\u0442\u043A\u0440\u044B\u0442\u044C \u043F\u0430\u043F\u043A\u0443 \u043C\u043E\u0434\u0430", L"Open mod folder")
                        : L10N(L"\u041E\u0442\u043A\u0440\u044B\u0442\u044C main.lua", L"Open main.lua");
                    g_tooltipVisible = true;
                }
            }
        }
        // Плавная подсветка карточки (hover-переход 120 мс).
        {
            int cardIdx = CardIndexAt(pt);
            if (cardIdx != g_hoverCardIdx) {
                g_hoverCardIdx = cardIdx;
                SetTimer(hwnd, kHoverTimerId, 15, nullptr);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            g_hoverWasCard = (cardIdx >= 0);
        }
        // Подсказка «перетаскивание отключено» при активном фильтре поиска.
        {
            bool wantHint = (!g_searchQuery.empty() && g_hoverActionBtn == 0 && g_hoverCardIdx >= 0);
            if (wantHint) {
                Layout hl = ComputeLayout(g_clientW, g_clientH);
                int hH = hl.list.bottom - hl.list.top;
                int hT = static_cast<int>(g_visible.size()) * kCardStep;
                int hW = (hl.list.right - hl.list.left) - (hT > hH ? (kScrollW + 8) : 0);
                int hy = hl.list.top + 4 - g_scroll + g_hoverCardIdx * kCardStep;
                g_tooltipAnchor = RECT{ hl.list.left, hy, hl.list.left + hW, hy + kCardH };
                g_tooltipText = L10N(L"\u041F\u0435\u0440\u0435\u0442\u0430\u0441\u043A\u0438\u0432\u0430\u043D\u0438\u0435 \u043E\u0442\u043A\u043B\u044E\u0447\u0435\u043D\u043E \u043F\u0440\u0438 \u043F\u043E\u0438\u0441\u043A\u0435",
                                     L"Drag disabled while searching");
                if (!g_tooltipVisible) { g_tooltipVisible = true; InvalidateRect(hwnd, nullptr, TRUE); }
            } else if (g_hoverActionBtn == 0 && g_tooltipVisible) {
                g_tooltipVisible = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        // Hover поля поиска.
        {
            bool hSearch = PointIn(g_rcSearch, pt);
            if (hSearch != g_hoverSearch) {
                g_hoverSearch = hSearch;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_trackingMouse = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        g_hoverLaunch = g_hoverInject = g_hoverSave = g_hoverLang = false;
        g_hoverSearch = false;
        g_hoverActionBtn = 0;
        g_hoverActionCard = -1;
        g_tooltipVisible = false;
        g_down = false;
        if (g_hoverCardIdx >= 0) {
            g_hoverCardIdx = -1;
            SetTimer(hwnd, kHoverTimerId, 15, nullptr);
        }
        g_hoverWasCard = false;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        // high-res wheel: use delta directly, 40px per notch
        g_scroll -= delta * 40 / WHEEL_DELTA;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        g_down = true; // pressed-состояние кнопок
        OnLeftDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;
    }
    case WM_LBUTTONUP: {
        g_down = false;
        bool pending = g_dragState.pendingClick;
        bool dragging = g_dragState.dragging;
        if (pending && !dragging) {
            int idx = g_dragState.pendingIndex;   // видимый индекс
            if (idx >= 0 && idx < static_cast<int>(g_visible.size())) {
                g_mods[g_visible[idx]].enabled = !g_mods[g_visible[idx]].enabled;
                g_dirty = true;
            }
        }
        if (dragging) {
            g_dirty = true; // произошёл реордер — помечаем к сохранению
        }
        g_dragState = DragState{};
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) {
            g_dragState = DragState{};
        }
        return 0;
    case WM_ERASEBKGND:
        return 1; // all painting is double-buffered; kill resize flicker
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, g_clientW, g_clientH);
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
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_TIMER:
        if (wParam == kToastTimerId) {
            KillTimer(hwnd, kToastTimerId);
            SetStatusKey(StatusKey::Ready);
            return 0;
        }
        if (wParam == kHoverTimerId) {
            // Плавный переход hover-подсветки карточки за ~120 мс (шаг ~15 мс).
            float target = (g_hoverCardIdx >= 0) ? 1.f : 0.f;
            float step = 15.f / static_cast<float>(Tok::HoverMs);
            if (g_hoverFade < target) g_hoverFade = std::min(target, g_hoverFade + step);
            else                      g_hoverFade = std::max(target, g_hoverFade - step);
            InvalidateRect(hwnd, nullptr, TRUE);
            if (std::abs(g_hoverFade - target) < (step / 2.f)) {
                g_hoverFade = target;
                KillTimer(hwnd, kHoverTimerId);
            }
            return 0;
        }
        return 0;
    case WM_APP_LAUNCH_DONE: {
        g_launching = false;
        bool ok = (bool)wParam;
        DWORD pid = (DWORD)lParam;
        if (ok && pid) {
            g_gamePid = pid; g_gameName = kGameProcess;
            g_appState = AppState::Injected; SetStatusKey(StatusKey::Injected);
        } else if (pid) {
            g_gamePid = pid; g_appState = AppState::StatusError; SetStatusKey(StatusKey::InjectFail);
        } else {
            g_appState = AppState::StatusError; SetStatusKey(StatusKey::NotFound);
        }
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
            g_gamePid = pid; g_gameName = kGameProcess;
            g_appState = AppState::Injected; SetStatusKey(StatusKey::Injected);
            MessageBoxW(hwnd,
                        L"LuaAPI.dll \u0443\u0441\u043F\u0435\u0448\u043D\u043E \u0432\u043D\u0435\u0434\u0440\u0435\u043D \u0432 \u0438\u0433\u0440\u0443!",
                        L"\u0423\u0441\u043F\u0435\u0445", MB_ICONINFORMATION | MB_OK);
        } else {
            g_appState = AppState::StatusError; SetStatusKey(StatusKey::InjectFail);
            MessageBoxW(hwnd, (L"\u0412\u043D\u0435\u0434\u0440\u0435\u043D\u0438\u0435 \u043D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C:\n" + err).c_str(),
                        L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE: {
        if (g_dirty) {
            // silent auto-save per spec (no MessageBox)
            SaveMods();
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DPICHANGED: {
        // wParam loword = new DPI x, hiword = y
        RECT* const prc = reinterpret_cast<RECT*>(lParam);
        // Recreate fonts from tokens scaled to new DPI
        RecreateFonts();
        g_clientW = prc->right - prc->left; g_clientH = prc->bottom - prc->top;
        RecalcLayout(); ClampScroll();
        SetWindowPos(hwnd, nullptr, prc->left, prc->top, g_clientW, g_clientH, SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kToastTimerId);
        KillTimer(hwnd, kHoverTimerId);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

namespace {


} // namespace

// Headless diagnostic launch: injector.exe --noinject
// Spawns gamemd.exe (no LuaAPI.dll), waits for exit, logs to injector_log.txt.
int RunNoInjectDiagnostic() {
    LogLine(L"--- Diagnostic (--noinject): pure vanilla launch ---");
    std::wstring exeDir = GetExeDirectory();
    std::wstring gamePath = exeDir + L"\\gamemd.exe";
    if (!FileExists(gamePath)) {
        LogLine(L"Diagnostics: gamemd.exe not found");
        MessageBoxW(nullptr, (L"\u0424\u0430\u0439\u043B \u043D\u0435 \u043D\u0430\u0439\u0434\u0435\u043D:\n" + gamePath).c_str(),
                    L"\u041E\u0448\u0438\u0431\u043A\u0430", MB_ICONERROR | MB_OK);
        return 1;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(gamePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, exeDir.c_str(), &si, &pi)) {
        LogLine(L"Diagnostics: CreateProcessW FAILED");
        return 1;
    }
    LogLine(L"Diagnostics: spawned suspended, resuming...");
    ResumeThread(pi.hThread);

    DWORD startTick = GetTickCount64();
    WaitForSingleObject(pi.hProcess, 30000);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    DWORD elapsed = GetTickCount64() - startTick;
    wchar_t b[16];
    swprintf(b, 16, L"%08X", code);
    LogLine(std::wstring(L"Diagnostics: exited code=0x") + b +
            L" after " + std::to_wstring(elapsed) + L" ms");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // High-DPI awareness: Per-Monitor DPI Aware V2 (fallback to system DPI aware)
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *SetDPIAwarenessContext)(HANDLE);
        SetDPIAwarenessContext setDPI = (SetDPIAwarenessContext)GetProcAddress(shcore, "SetProcessDpiAwarenessContext");
        if (setDPI) {
            setDPI(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            // Fallback: SetProcessDPIAware
            SetProcessDPIAware();
        }
        FreeLibrary(shcore);
    } else {
        SetProcessDPIAware();
    }

    // Headless diagnostic / compatibility modes
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--noinject") == 0) {
                g_skipInjection = true;
                g_headless = true;
            } else if (_wcsicmp(argv[i], L"--withcncnet") == 0) {
                g_injectCnCNet = true;
                g_headless = true;
            } else if (_wcsnicmp(argv[i], L"--attach=", 9) == 0) {
                g_attachMode = true;
                g_headless = true;
                g_attachTarget = argv[i] + 9;
            } else if (_wcsicmp(argv[i], L"--attach") == 0) {
                g_attachMode = true;
                g_headless = true;
                // Опциональный явный аргумент: --attach gamemd.exe
                if (i + 1 < argc && argv[i + 1][0] != L'-') {
                    g_attachTarget = argv[i + 1];
                    ++i;
                }
            }
        }
        if (argv) LocalFree(argv);

        // LUAAPI_ATTACH=1 (и совместимый ATTACH_MODE=1): клиент (например CnCNet)
        // запускает игру сам — ждать целевой процесс.
        if (!g_attachMode) {
            char envAttach[2] = {0};
            GetEnvironmentVariableA("LUAAPI_ATTACH", envAttach, sizeof(envAttach));
            if (envAttach[0] == '1') {
                g_attachMode = true;
                g_headless = true;
            } else {
                GetEnvironmentVariableA("ATTACH_MODE", envAttach, sizeof(envAttach));
                if (envAttach[0] == '1') {
                    g_attachMode = true;
                    g_headless = true;
                }
            }
        }

        if (g_headless) {
            if (g_attachMode) {
                return RunAttachWait(g_attachTarget);
            }
            LogLine(L"--- Headless launch started ---");
            DoLaunchGame();
            // Wait for the game process to exit, then cleanly exit the injector.
            // Use SYNCHRONIZE so we can wait without needing PROCESS_TERMINATE rights.
            if (g_gamePid != 0) {
                HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, g_gamePid);
                if (!hProcess) hProcess = OpenProcess(SYNCHRONIZE, FALSE, g_gamePid);
                if (hProcess) {
                    LogLine(L"Headless: waiting for gamemd.exe (PID " + std::to_wstring(g_gamePid) + L") to exit...");
                    WaitForSingleObject(hProcess, INFINITE);
                    CloseHandle(hProcess);
                    LogLine(L"Headless: game exited, injector terminating");
                } else {
                    LogLine(L"Headless: OpenProcess failed, cannot wait for game exit");
                }
            } else {
                LogLine(L"Headless: g_gamePid == 0, game never appeared");
            }
            // Give a small grace period after game exit for log flush
            Sleep(500);
            return 0;
        }
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);

    // Compute window size from desired client area (580 x 640).
    RECT rc{ 0, 0, kDefaultClientW, kDefaultClientH };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, FALSE, 0);
    int wndW = rc.right - rc.left;
    int wndH = rc.bottom - rc.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - wndW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - wndH) / 2;

    g_hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
                             x, y, wndW, wndH,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd)
        return 1;

    RecreateFonts();

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_fontTitle);
    DeleteObject(g_fontCard);
    DeleteObject(g_fontBody);
    DeleteObject(g_fontSmall);
    return static_cast<int>(msg.wParam);
}
