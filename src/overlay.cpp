#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <D2RLPlugin/api.h>
#include <itemdb/overlay.hpp>
#include <itemdb/prototype.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace itemdb {
namespace {
constexpr wchar_t OverlayClass[] = L"D2RRItemDatabaseOverlay";
constexpr wchar_t CursorClass[] = L"D2RRItemDatabaseCursor";
constexpr UINT ToggleMessage = WM_APP + 0x241;
constexpr UINT StopMessage = WM_APP + 0x242;
constexpr UINT CursorMoveMessage = WM_APP + 0x243;
constexpr UINT TrackingTimer = 1;
constexpr UINT CursorTimer = 2;
constexpr int SearchId = 4100;
constexpr int ComboFirstId = 4110;
constexpr int HideVanillaId = 4120;
constexpr int ExactRunesId = 4121;
constexpr int ResetFiltersId = 4122;
constexpr int RuneListId = 4123;
constexpr int HideVanillaLabelId = 4124;
constexpr int ExactRunesLabelId = 4125;
constexpr COLORREF ParchmentText = RGB(238, 233, 217);
constexpr COLORREF SiteUniqueText = RGB(199, 183, 144);
constexpr COLORREF SiteSetText = RGB(76, 194, 56);
constexpr COLORREF SearchHintText = RGB(199, 183, 144);
constexpr COLORREF SiteMagicText = RGB(99, 119, 239);
constexpr COLORREF SiteBaseText = RGB(181, 181, 181);
constexpr COLORREF SiteRequirementText = RGB(233, 107, 99);
constexpr COLORREF SiteRarityText = RGB(99, 199, 239);
constexpr COLORREF SiteRuneText = RGB(247, 241, 227);
constexpr COLORREF SiteSocketText = RGB(188, 167, 125);

HCURSOR arrowCursor() {
    static const HCURSOR cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    return cursor;
}

LRESULT CALLBACK controlSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR refData) {
    if (message == WM_SETCURSOR) {
        SetCursor(reinterpret_cast<HCURSOR>(refData));
        return TRUE;
    }
    if (message == WM_MOUSEMOVE) {
        const HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root != nullptr) SendMessageW(root, CursorMoveMessage, 0, 0);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, controlSubclassProc, 1);
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK searchSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR, DWORD_PTR refData) {
    if (message == WM_SETCURSOR) {
        SetCursor(reinterpret_cast<HCURSOR>(refData));
        return TRUE;
    }
    if (message == WM_MOUSEMOVE) {
        const HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root != nullptr) SendMessageW(root, CursorMoveMessage, 0, 0);
    }
    const LRESULT result = DefSubclassProc(hwnd, message, wparam, lparam);
    if (message == WM_PAINT && GetWindowTextLengthW(hwnd) == 0) {
        const HDC dc = GetDC(hwnd);
        if (dc != nullptr) {
            RECT client{};
            GetClientRect(hwnd, &client);
            client.left += 5;
            const auto oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
            SetTextColor(dc, SearchHintText);
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, L"Search name, property, base, or class...", -1, &client,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);
            ReleaseDC(hwnd, dc);
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, searchSubclassProc, 1);
    return result;
}

LRESULT CALLBACK cursorWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        const HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(dc, &client, key);
        DeleteObject(key);
        DrawIconEx(dc, 0, 0, arrowCursor(), 32, 32, 0, nullptr, DI_NORMAL);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string number(double value) {
    if (std::floor(value) == value) return std::to_string(static_cast<long long>(value));
    std::ostringstream output;
    output.precision(1);
    output << std::fixed << value;
    return output.str();
}

void addField(std::vector<std::string>& lines, const Record& record, const char* key, const char* label) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end() || found->second.empty()) return;
    std::string value;
    for (const auto& part : found->second) {
        if (!value.empty()) value += ", ";
        value += part;
    }
    lines.emplace_back(std::string(label) + ": " + value);
}

void addNumber(std::vector<std::string>& lines, const Record& record, const char* key, const char* label, bool positiveOnly = false) {
    const auto found = record.numbers.find(key);
    if (found == record.numbers.end() || (positiveOnly && found->second <= 0)) return;
    lines.emplace_back(std::string(label) + ": " + number(found->second));
}

std::vector<std::string> recordLines(const Record& record, bool includeProperties = true) {
    std::vector<std::string> lines;
    addField(lines, record, "base", "Base");
    addField(lines, record, "set", "Set");
    addField(lines, record, "runes", "Runes");
    addField(lines, record, "type", "Item Type");
    addField(lines, record, "category", "Category");
    addField(lines, record, "tier", "Tier");
    addNumber(lines, record, "required_level", "Required Level");
    addNumber(lines, record, "strength", "Required Strength", true);
    addNumber(lines, record, "dexterity", "Required Dexterity", true);
    addField(lines, record, "class", "Class");
    addField(lines, record, "weapon_type", "Weapon Type");
    const auto minimum = record.numbers.find("min_damage");
    const auto maximum = record.numbers.find("max_damage");
    if (minimum != record.numbers.end() && maximum != record.numbers.end())
        lines.emplace_back("Damage: " + number(minimum->second) + "-" + number(maximum->second));
    addNumber(lines, record, "avg_damage", "Average Damage");
    addNumber(lines, record, "defense", "Defense");
    const auto sockets = record.numbers.find("sockets");
    if (sockets != record.numbers.end()) lines.emplace_back("Sockets: " + number(sockets->second));
    else addNumber(lines, record, "max_sockets", "Maximum Sockets");
    addField(lines, record, "origin", "Origin");
    if (includeProperties) {
        bool heading = false;
        for (const auto& property : record.properties) {
            if (property.text.empty()) continue;
            if (!heading) { lines.emplace_back("Properties:"); heading = true; }
            lines.push_back(property.text);
        }
    }
    return lines;
}

struct Layout {
    RECT close{};
    std::array<RECT, 4> tabs{};
    std::array<RECT, PrototypePageSize> rows{};
    RECT previous{}, next{}, detail{};
};

struct ScrollMetrics {
    bool visible = false;
    int maximum = 0;
    RECT track{};
    RECT thumb{};
};

Layout layoutFor(int width, int height) {
    Layout layout;
    layout.close = {width - 58, 16, width - 18, 56};
    const int tabLeft = 32, tabRight = width - 32, tabTop = 78, tabHeight = 58;
    const int tabWidth = (tabRight - tabLeft) / 4;
    for (int tab = 0; tab < 4; ++tab)
        layout.tabs[static_cast<size_t>(tab)] = {tabLeft + tab * tabWidth, tabTop, tabLeft + (tab + 1) * tabWidth, tabTop + tabHeight};
    const int listLeft = 38;
    const int listWidth = std::clamp(width * 27 / 100, 300, 430);
    const int rowsTop = 280;
    const int navigationHeight = 92;
    const int available = std::max(288, height - rowsTop - navigationHeight - 30);
    const int rowHeight = std::clamp(available / static_cast<int>(PrototypePageSize), 36, 58);
    for (int row = 0; row < static_cast<int>(PrototypePageSize); ++row)
        layout.rows[static_cast<size_t>(row)] = {listLeft, rowsTop + row * rowHeight, listLeft + listWidth, rowsTop + (row + 1) * rowHeight - 4};
    const int navTop = rowsTop + static_cast<int>(PrototypePageSize) * rowHeight + 4;
    layout.previous = {listLeft, navTop, listLeft + listWidth / 2 - 3, navTop + 46};
    layout.next = {listLeft + listWidth / 2 + 3, navTop, listLeft + listWidth, navTop + 46};
    layout.detail = {listLeft + listWidth + 28, 270, width - 40, height - 35};
    return layout;
}

bool contains(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

struct WindowCandidate { HWND excluded = nullptr; HWND best = nullptr; long long area = 0; };

BOOL CALLBACK findWindow(HWND window, LPARAM value) {
    auto& candidate = *reinterpret_cast<WindowCandidate*>(value);
    if (window == candidate.excluded || !IsWindowVisible(window) || GetAncestor(window, GA_ROOT) != window) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const long long area = static_cast<long long>(client.right - client.left) * (client.bottom - client.top);
    if (area > candidate.area) { candidate.area = area; candidate.best = window; }
    return TRUE;
}

HWND findGameWindow(HWND overlay) {
    WindowCandidate candidate{overlay};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&candidate));
    return candidate.best;
}
}

struct OverlayHost::Impl {
    PrototypeViewModel& model;
    const D2RL::PluginContext* plugin = nullptr;
    HANDLE thread = nullptr;
    HANDLE ready = nullptr;
    std::atomic<HWND> window{nullptr};
    bool visible = false;
    int detailScroll = 0;
    bool draggingScroll = false;
    int scrollDragOffset = 0;
    HWND gameWindow = nullptr;
    HWND searchEdit = nullptr;
    std::array<HWND, 7> filterCombos{};
    HWND hideVanilla = nullptr;
    HWND hideVanillaLabel = nullptr;
    HWND exactRunes = nullptr;
    HWND exactRunesLabel = nullptr;
    HWND resetFilters = nullptr;
    HWND runeListLabel = nullptr;
    HWND runeList = nullptr;
    std::vector<std::string> runeListValues;
    HFONT controlFont = nullptr;
    HBRUSH controlBrush = nullptr;
    bool updatingControls = false;
    int cursorVisibilityAdjustments = 0;
    HCURSOR transparentCursor = nullptr;
    HWND cursorWindow = nullptr;
    bool softwareCursorShown = false;
    bool dropdownActive = false;
    POINT lastCursorPosition{-1, -1};
    std::array<std::string, 7> comboKeys{};
    std::array<std::vector<std::string>, 7> comboValues{};

    Impl(PrototypeViewModel& source, const D2RL::PluginContext* context) : model(source), plugin(context) {}

    static DWORD WINAPI entry(void* value) noexcept {
        static_cast<Impl*>(value)->run();
        return 0;
    }

    bool start() {
        ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ready == nullptr) return false;
        thread = CreateThread(nullptr, 0, entry, this, 0, nullptr);
        if (thread == nullptr) { CloseHandle(ready); ready = nullptr; return false; }
        if (WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0 || window.load() == nullptr) {
            stop();
            return false;
        }
        return true;
    }

    void stop() noexcept {
        if (const HWND handle = window.load()) PostMessageW(handle, StopMessage, 0, 0);
        if (thread != nullptr) {
            WaitForSingleObject(thread, 5000);
            CloseHandle(thread);
            thread = nullptr;
        }
        if (ready != nullptr) { CloseHandle(ready); ready = nullptr; }
        window = nullptr;
    }

    void toggle() noexcept {
        if (const HWND handle = window.load()) PostMessageW(handle, ToggleMessage, 0, 0);
    }

    void run() noexcept {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW cls{sizeof(cls)};
        cls.style = CS_HREDRAW | CS_VREDRAW;
        cls.lpfnWndProc = windowProc;
        cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cls.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        cls.lpszClassName = OverlayClass;
        RegisterClassExW(&cls);
        WNDCLASSEXW cursorClass{sizeof(cursorClass)};
        cursorClass.lpfnWndProc = cursorWindowProc;
        cursorClass.hInstance = instance;
        cursorClass.lpszClassName = CursorClass;
        RegisterClassExW(&cursorClass);
        gameWindow = findGameWindow(nullptr);
        const HWND handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, OverlayClass, L"D2RR Item Database",
            WS_POPUP, 100, 100, 1200, 760, gameWindow, nullptr, instance, this);
        window = handle;
        SetEvent(ready);
        if (handle == nullptr) return;
        std::array<BYTE, 32 * 4> andMask{};
        std::array<BYTE, 32 * 4> xorMask{};
        andMask.fill(0xff);
        transparentCursor = CreateCursor(instance, 0, 0, 32, 32, andMask.data(), xorMask.data());
        cursorWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
            WS_EX_TOPMOST | WS_EX_NOACTIVATE, CursorClass, L"", WS_POPUP,
            0, 0, 32, 32, handle, nullptr, instance, nullptr);
        if (cursorWindow != nullptr)
            SetLayeredWindowAttributes(cursorWindow, RGB(255, 0, 255), 255, LWA_COLORKEY);
        createControls(handle);
        syncControls();
        SetTimer(handle, TrackingTimer, 100, nullptr);
        SetTimer(handle, CursorTimer, 50, nullptr);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            const bool closeKey = message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE;
            const bool hotkey = (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
                message.wParam == 'S' && (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (message.hwnd != handle && (closeKey || hotkey)) {
                PostMessageW(handle, WM_KEYDOWN, VK_ESCAPE, 0);
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (controlFont != nullptr) DeleteObject(controlFont);
        if (controlBrush != nullptr) DeleteObject(controlBrush);
        if (transparentCursor != nullptr) DestroyCursor(transparentCursor);
        window = nullptr;
        UnregisterClassW(CursorClass, instance);
        UnregisterClassW(OverlayClass, instance);
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
        switch (message) {
        case ToggleMessage:
            self->visible = !self->visible;
            self->detailScroll = 0;
            self->updatePlacement(true);
            if (self->visible) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
                SetFocus(self->searchEdit);
                self->hideSystemCursor();
                self->maintainCursor();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else self->hideOverlay();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;
        case StopMessage:
            DestroyWindow(hwnd);
            return 0;
        case WM_TIMER:
            if (wparam == TrackingTimer) self->updatePlacement(false);
            else if (wparam == CursorTimer) self->maintainCursor();
            return 0;
        case CursorMoveMessage:
            self->maintainCursor();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->paint(hwnd);
            return 0;
        case WM_SIZE:
            self->positionControls();
            return 0;
        case WM_COMMAND:
            self->controlChanged(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC: {
            const HDC controlDc = reinterpret_cast<HDC>(wparam);
            SetTextColor(controlDc, ParchmentText);
            SetBkColor(controlDc, RGB(48, 47, 44));
            return reinterpret_cast<LRESULT>(self->controlBrush);
        }
        case WM_LBUTTONDOWN:
            if (self->beginScrollDrag(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) return 0;
            break;
        case WM_MOUSEMOVE:
            self->maintainCursor();
            if (self->draggingScroll) {
                self->continueScrollDrag(GET_Y_LPARAM(lparam));
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (self->draggingScroll) {
                self->draggingScroll = false;
                ReleaseCapture();
                return 0;
            }
            self->click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_CAPTURECHANGED:
            self->draggingScroll = false;
            return 0;
        case WM_SETCURSOR:
            SetCursor(self->transparentCursor);
            return TRUE;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            RECT client{}; GetClientRect(hwnd, &client);
            const RECT detail = layoutFor(client.right, client.bottom).detail;
            if (contains(detail, point.x, point.y)) {
                const int maximum = self->currentScrollMetrics().maximum;
                self->detailScroll = std::clamp(self->detailScroll + (GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -3 : 3), 0, maximum);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) { self->hideOverlay(); return 0; }
            if (wparam == VK_LEFT && self->model.previousPage()) { self->detailScroll = 0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wparam == VK_RIGHT && self->model.nextPage()) { self->detailScroll = 0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            break;
        case WM_SYSKEYDOWN:
            if (wparam == 'S' && (GetKeyState(VK_MENU) & 0x8000) != 0) { self->hideOverlay(); return 0; }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, TrackingTimer);
            KillTimer(hwnd, CursorTimer);
            self->restoreCursorVisibility();
            if (self->cursorWindow != nullptr) {
                DestroyWindow(self->cursorWindow);
                self->cursorWindow = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void hideOverlay() {
        visible = false;
        draggingScroll = false;
        dropdownActive = false;
        if (GetCapture() == window.load()) ReleaseCapture();
        ShowWindow(window.load(), SW_HIDE);
        if (cursorWindow != nullptr) ShowWindow(cursorWindow, SW_HIDE);
        softwareCursorShown = false;
        restoreCursorVisibility();
        if (IsWindow(gameWindow)) SetForegroundWindow(gameWindow);
    }

    void hideSystemCursor() {
        if (cursorVisibilityAdjustments != 0) return;
        int count = ShowCursor(FALSE);
        ++cursorVisibilityAdjustments;
        while (count >= 0) {
            count = ShowCursor(FALSE);
            ++cursorVisibilityAdjustments;
        }
        SetCursor(transparentCursor);
    }

    void restoreCursorVisibility() {
        while (cursorVisibilityAdjustments > 0) {
            ShowCursor(TRUE);
            --cursorVisibilityAdjustments;
        }
    }

    void maintainCursor() {
        if (!visible || !IsWindowVisible(window.load()) || cursorWindow == nullptr) return;
        if (dropdownActive) {
            if (softwareCursorShown) ShowWindow(cursorWindow, SW_HIDE);
            softwareCursorShown = false;
            SetCursor(arrowCursor());
            return;
        }
        POINT point{};
        RECT overlayRect{};
        if (GetCursorPos(&point) && GetWindowRect(window.load(), &overlayRect) &&
            PtInRect(&overlayRect, point)) {
            SetCursor(transparentCursor);
            if (point.x != lastCursorPosition.x || point.y != lastCursorPosition.y) {
                SetWindowPos(cursorWindow, nullptr, point.x, point.y, 0, 0,
                    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
                lastCursorPosition = point;
            }
            if (!softwareCursorShown) {
                ShowWindow(cursorWindow, SW_SHOWNOACTIVATE);
                softwareCursorShown = true;
            }
        } else if (softwareCursorShown) {
            ShowWindow(cursorWindow, SW_HIDE);
            softwareCursorShown = false;
        }
    }

    void beginNativeDropdown() {
        dropdownActive = true;
        if (cursorWindow != nullptr) ShowWindow(cursorWindow, SW_HIDE);
        softwareCursorShown = false;
        restoreCursorVisibility();
        SetCursor(arrowCursor());
    }

    void endNativeDropdown() {
        dropdownActive = false;
        if (visible) {
            hideSystemCursor();
            maintainCursor();
        }
    }

    void createControls(HWND parent) {
        controlFont = font(16);
        controlBrush = CreateSolidBrush(RGB(48, 47, 44));
        searchEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 100, 30, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(SearchId)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(searchEdit, EM_SETLIMITTEXT, 120, 0);
        SetWindowSubclass(searchEdit, searchSubclassProc, 1, reinterpret_cast<DWORD_PTR>(transparentCursor));
        for (size_t index = 0; index < filterCombos.size(); ++index) {
            filterCombos[index] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | WS_BORDER |
                CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
                0, 0, 100, 260, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ComboFirstId + static_cast<int>(index))), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(filterCombos[index], WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
            SetWindowSubclass(filterCombos[index], controlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(transparentCursor));
            COMBOBOXINFO info{sizeof(info)};
            if (GetComboBoxInfo(filterCombos[index], &info) && info.hwndList != nullptr)
                SetWindowSubclass(info.hwndList, controlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(arrowCursor()));
        }
        hideVanilla = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 22, 28, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(HideVanillaId)), GetModuleHandleW(nullptr), nullptr);
        hideVanillaLabel = CreateWindowExW(0, L"STATIC", L"HIDE VANILLA", WS_CHILD | SS_NOTIFY,
            0, 0, 130, 28, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(HideVanillaLabelId)), GetModuleHandleW(nullptr), nullptr);
        exactRunes = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 22, 28, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExactRunesId)), GetModuleHandleW(nullptr), nullptr);
        exactRunesLabel = CreateWindowExW(0, L"STATIC", L"EXACT RUNES", WS_CHILD | SS_NOTIFY,
            0, 0, 130, 28, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExactRunesLabelId)), GetModuleHandleW(nullptr), nullptr);
        resetFilters = CreateWindowExW(0, L"BUTTON", L"RESET FILTERS", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 130, 30, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ResetFiltersId)), GetModuleHandleW(nullptr), nullptr);
        runeListLabel = CreateWindowExW(0, L"STATIC", L"SELECT RUNES (MULTIPLE)", WS_CHILD,
            0, 0, 100, 20, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        runeList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
            LBS_MULTIPLESEL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 100, 90, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(RuneListId)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hideVanilla, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(hideVanillaLabel, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(exactRunes, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(exactRunesLabel, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(resetFilters, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(runeListLabel, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        SendMessageW(runeList, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        for (const HWND control : {hideVanilla, hideVanillaLabel, exactRunes, exactRunesLabel,
                                   resetFilters, runeListLabel, runeList})
            SetWindowSubclass(control, controlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(transparentCursor));
    }

    void configureCombo(size_t index, std::string key, const std::string& allLabel,
        std::vector<std::string> values, const std::string& selected, const std::vector<std::string>& displayValues = {}) {
        comboKeys[index] = std::move(key);
        comboValues[index].clear();
        comboValues[index].push_back({});
        comboValues[index].insert(comboValues[index].end(), values.begin(), values.end());
        SendMessageW(filterCombos[index], CB_RESETCONTENT, 0, 0);
        SendMessageW(filterCombos[index], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(allLabel).c_str()));
        int selectedIndex = 0;
        for (size_t item = 0; item < values.size(); ++item) {
            const auto& display = displayValues.size() == values.size() ? displayValues[item] : values[item];
            SendMessageW(filterCombos[index], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(display).c_str()));
            if (lower(values[item]) == lower(selected)) selectedIndex = static_cast<int>(item + 1);
        }
        SendMessageW(filterCombos[index], CB_SETCURSEL, selectedIndex, 0);
        ShowWindow(filterCombos[index], SW_SHOWNA);
    }

    void syncControls() {
        if (searchEdit == nullptr) return;
        updatingControls = true;
        const size_t tab = model.activeTab();
        const auto& filter = model.filters(tab);
        SetWindowTextW(searchEdit, wide(filter.text).c_str());
        for (auto control : filterCombos) ShowWindow(control, SW_HIDE);
        ShowWindow(runeListLabel, SW_HIDE);
        ShowWindow(runeList, SW_HIDE);
        for (auto& key : comboKeys) key.clear();
        ShowWindow(hideVanilla, tab < 3 ? SW_SHOWNA : SW_HIDE);
        ShowWindow(hideVanillaLabel, tab < 3 ? SW_SHOWNA : SW_HIDE);
        ShowWindow(exactRunes, tab == 2 ? SW_SHOWNA : SW_HIDE);
        ShowWindow(exactRunesLabel, tab == 2 ? SW_SHOWNA : SW_HIDE);
        SendMessageW(hideVanilla, BM_SETCHECK, filter.hideVanilla ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(exactRunes, BM_SETCHECK, filter.exactRunes ? BST_CHECKED : BST_UNCHECKED, 0);
        const std::vector<std::string> weaponModes{"1h", "2h"};
        const std::vector<std::string> weaponModeLabels{"1H ONLY", "2H ONLY"};
        const std::vector<std::string> damageSorts{
            "avg-1h-phys-descending", "avg-1h-phys-ascending", "avg-2h-phys-descending", "avg-2h-phys-ascending",
            "avg-throw-phys-descending", "avg-throw-phys-ascending", "avg-non-phys-descending", "avg-non-phys-ascending"};
        const std::vector<std::string> damageSortLabels{
            "1H PHYSICAL - HIGHEST", "1H PHYSICAL - LOWEST", "2H PHYSICAL - HIGHEST", "2H PHYSICAL - LOWEST",
            "THROW PHYSICAL - HIGHEST", "THROW PHYSICAL - LOWEST", "ELEMENTAL - HIGHEST", "ELEMENTAL - LOWEST"};
        if (tab == 0 || tab == 1) {
            configureCombo(0, "type", "ALL ITEM TYPES", model.filterOptions(tab, "type"), filter.itemType);
            configureCombo(1, "equipment", "ALL EQUIPMENT", model.filterOptions(tab, "equipment"), filter.equipment);
            configureCombo(2, "class", "ALL CLASSES", model.filterOptions(tab, "class"), filter.itemClass);
            configureCombo(3, "weapon", "ALL WEAPONS", weaponModes, filter.weaponMode, weaponModeLabels);
            configureCombo(4, "sort", "DEFAULT DAMAGE SORT", damageSorts, filter.damageSort, damageSortLabels);
        } else if (tab == 2) {
            configureCombo(0, "type", "ALL ITEM TYPES", model.filterOptions(tab, "type"), filter.itemType);
            configureCombo(1, "rune_count", "ANY RUNE COUNT", {"2", "3", "4", "5", "6"},
                filter.runeCount == 0 ? "" : std::to_string(filter.runeCount), {"2 RUNES", "3 RUNES", "4 RUNES", "5 RUNES", "6 RUNES"});
            runeListValues = model.filterOptions(tab, "rune");
            SendMessageW(runeList, LB_RESETCONTENT, 0, 0);
            for (size_t index = 0; index < runeListValues.size(); ++index) {
                SendMessageW(runeList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(runeListValues[index]).c_str()));
                if (std::any_of(filter.runes.begin(), filter.runes.end(), [&](const std::string& rune) {
                    return lower(rune) == lower(runeListValues[index]);
                })) SendMessageW(runeList, LB_SETSEL, TRUE, static_cast<LPARAM>(index));
            }
            ShowWindow(runeListLabel, SW_SHOWNA);
            ShowWindow(runeList, SW_SHOWNA);
        } else {
            configureCombo(0, "category", "ALL BASES", {"Weapon", "Armor"}, filter.category, {"WEAPONS", "ARMORS"});
            configureCombo(1, "type", "ALL ITEM TYPES", model.filterOptions(tab, "type"), filter.itemType);
            configureCombo(2, "class", "ALL CLASSES", model.filterOptions(tab, "class"), filter.itemClass);
            configureCombo(3, "tier", "ALL TIERS", {"Normal", "Exceptional", "Elite"}, filter.tier);
            configureCombo(4, "sockets", "ANY SOCKETS", {"1", "2", "3", "4", "5", "6"},
                filter.sockets == 0 ? "" : std::to_string(filter.sockets), {"1 SOCKET", "2 SOCKETS", "3 SOCKETS", "4 SOCKETS", "5 SOCKETS", "6 SOCKETS"});
            configureCombo(5, "weapon", "ALL WEAPONS", weaponModes, filter.weaponMode, weaponModeLabels);
            configureCombo(6, "sort", "DEFAULT DAMAGE SORT", damageSorts, filter.damageSort, damageSortLabels);
        }
        positionControls();
        updatingControls = false;
    }

    void positionControls() {
        if (searchEdit == nullptr || window.load() == nullptr) return;
        RECT client{};
        GetClientRect(window.load(), &client);
        const Layout layout = layoutFor(client.right, client.bottom);
        const int listLeft = layout.rows[0].left;
        const int listWidth = layout.rows[0].right - layout.rows[0].left;
        MoveWindow(searchEdit, listLeft, 150, listWidth, 34, TRUE);
        MoveWindow(hideVanilla, listLeft, 190, 22, 28, TRUE);
        MoveWindow(hideVanillaLabel, listLeft + 25, 190, 130, 28, TRUE);
        MoveWindow(exactRunes, listLeft + 160, 190, 22, 28, TRUE);
        MoveWindow(exactRunesLabel, listLeft + 185, 190, 130, 28, TRUE);
        MoveWindow(resetFilters, listLeft + listWidth - 135, 218, 135, 30, TRUE);
        const int left = layout.detail.left;
        const int available = layout.detail.right - layout.detail.left;
        const int gap = 8;
        const int columns = 3;
        const int width = (available - gap * (columns - 1)) / columns;
        for (size_t index = 0; index < filterCombos.size(); ++index) {
            const int column = static_cast<int>(index) % columns;
            const int row = static_cast<int>(index) / columns;
            MoveWindow(filterCombos[index], left + column * (width + gap), 150 + row * 40, width, 300, TRUE);
        }
        MoveWindow(runeListLabel, left + 2 * (width + gap), 150, width, 20, TRUE);
        MoveWindow(runeList, left + 2 * (width + gap), 172, width, 88, TRUE);
    }

    void controlChanged(int id, int notification) {
        if (updatingControls) return;
        const bool combo = id >= ComboFirstId && id < ComboFirstId + static_cast<int>(filterCombos.size());
        if (combo && notification == CBN_DROPDOWN) {
            beginNativeDropdown();
            return;
        }
        if (combo && notification == CBN_CLOSEUP) {
            endNativeDropdown();
            return;
        }
        const size_t tab = model.activeTab();
        auto& filter = model.filters(tab);
        bool changed = false;
        if (id == SearchId && notification == EN_CHANGE) {
            const int length = GetWindowTextLengthW(searchEdit);
            std::wstring value(static_cast<size_t>(length + 1), L'\0');
            if (length > 0) GetWindowTextW(searchEdit, value.data(), length + 1);
            value.resize(static_cast<size_t>(length));
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), length, nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(bytes), '\0');
            if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), length, utf8.data(), bytes, nullptr, nullptr);
            filter.text = std::move(utf8);
            changed = true;
        } else if (combo && notification == CBN_SELCHANGE) {
            const size_t index = static_cast<size_t>(id - ComboFirstId);
            const int selection = static_cast<int>(SendMessageW(filterCombos[index], CB_GETCURSEL, 0, 0));
            const std::string value = selection >= 0 && static_cast<size_t>(selection) < comboValues[index].size() ? comboValues[index][static_cast<size_t>(selection)] : "";
            const auto& key = comboKeys[index];
            if (key == "type") filter.itemType = value;
            else if (key == "equipment") filter.equipment = value;
            else if (key == "class") filter.itemClass = value;
            else if (key == "weapon") filter.weaponMode = value;
            else if (key == "sort") filter.damageSort = value;
            else if (key == "category") filter.category = value;
            else if (key == "tier") filter.tier = value;
            else if (key == "sockets") filter.sockets = value.empty() ? 0 : std::stoi(value);
            else if (key == "rune_count") filter.runeCount = value.empty() ? 0 : std::stoi(value);
            changed = true;
        } else if (id == RuneListId && notification == LBN_SELCHANGE) {
            filter.runes.clear();
            for (size_t index = 0; index < runeListValues.size(); ++index)
                if (SendMessageW(runeList, LB_GETSEL, static_cast<WPARAM>(index), 0) > 0) filter.runes.push_back(runeListValues[index]);
            changed = true;
        } else if (id == HideVanillaId && notification == BN_CLICKED) {
            filter.hideVanilla = SendMessageW(hideVanilla, BM_GETCHECK, 0, 0) == BST_CHECKED;
            changed = true;
        } else if (id == HideVanillaLabelId && notification == STN_CLICKED) {
            SendMessageW(hideVanilla, BM_CLICK, 0, 0);
            return;
        } else if (id == ExactRunesId && notification == BN_CLICKED) {
            filter.exactRunes = SendMessageW(exactRunes, BM_GETCHECK, 0, 0) == BST_CHECKED;
            changed = true;
        } else if (id == ExactRunesLabelId && notification == STN_CLICKED) {
            SendMessageW(exactRunes, BM_CLICK, 0, 0);
            return;
        } else if (id == ResetFiltersId && notification == BN_CLICKED) {
            model.resetFilters(tab);
            detailScroll = 0;
            syncControls();
            InvalidateRect(window.load(), nullptr, FALSE);
            return;
        }
        if (changed) {
            model.applyFilters(tab);
            detailScroll = 0;
            InvalidateRect(window.load(), nullptr, FALSE);
        }
    }

    void updatePlacement(bool forceShow) {
        if (!IsWindow(gameWindow)) gameWindow = findGameWindow(window.load());
        if (!IsWindow(gameWindow)) { if (visible) ShowWindow(window.load(), SW_HIDE); return; }
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        if (!visible || foregroundProcess != GetCurrentProcessId()) {
            if (!forceShow) ShowWindow(window.load(), SW_HIDE);
            return;
        }
        RECT client{};
        if (!GetClientRect(gameWindow, &client)) return;
        POINT origin{0, 0};
        ClientToScreen(gameWindow, &origin);
        const int gameWidth = client.right - client.left;
        const int gameHeight = client.bottom - client.top;
        // Keep the overlay compact on ultrawide and high-resolution clients.
        // These caps still leave enough room for the three-column base view.
        const int width = std::min(gameWidth, std::clamp(gameWidth * 78 / 100, 1050, 1500));
        const int height = std::min(gameHeight, std::clamp(gameHeight * 86 / 100, 720, 920));
        const int x = origin.x + (gameWidth - width) / 2;
        const int y = origin.y + (gameHeight - height) / 2;
        if (GetWindow(window.load(), GW_OWNER) != gameWindow)
            SetWindowLongPtrW(window.load(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
        RECT current{};
        const bool hasCurrent = GetWindowRect(window.load(), &current) != FALSE;
        const bool geometryChanged = !hasCurrent || current.left != x || current.top != y ||
            current.right - current.left != width || current.bottom - current.top != height;
        const bool needsShow = visible && !IsWindowVisible(window.load());
        if (geometryChanged || needsShow || forceShow) {
            UINT flags = SWP_NOACTIVATE;
            if (needsShow || forceShow) flags |= SWP_SHOWWINDOW;
            SetWindowPos(window.load(), HWND_TOPMOST, x, y, width, height, flags);
        }
    }

    HFONT font(int pixels, int weight = FW_NORMAL) const {
        return CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Palatino Linotype");
    }

    void text(HDC dc, const std::string& value, RECT rect, COLORREF color, int pixels, UINT flags, int weight = FW_NORMAL) const {
        const auto converted = wide(value);
        const HFONT selected = font(pixels, weight);
        const auto old = SelectObject(dc, selected);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, converted.c_str(), static_cast<int>(converted.size()), &rect, flags);
        SelectObject(dc, old);
        DeleteObject(selected);
    }

    void button(HDC dc, const RECT& rect, const std::string& label, bool selected, bool enabled = true,
        COLORREF labelColor = ParchmentText) const {
        const HBRUSH fill = CreateSolidBrush(selected ? RGB(70, 67, 58) : RGB(48, 47, 44));
        FillRect(dc, &rect, fill); DeleteObject(fill);
        const HPEN outer = CreatePen(PS_SOLID, 2, enabled ? RGB(178, 132, 62) : RGB(82, 76, 65));
        const auto oldPen = SelectObject(dc, outer);
        const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        RECT inner{rect.left + 4, rect.top + 4, rect.right - 4, rect.bottom - 4};
        const HPEN innerPen = CreatePen(PS_SOLID, 1, enabled ? RGB(105, 82, 44) : RGB(65, 61, 55));
        SelectObject(dc, innerPen); Rectangle(dc, inner.left, inner.top, inner.right, inner.bottom);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(innerPen); DeleteObject(outer);
        RECT labelRect = rect;
        text(dc, label, labelRect, enabled ? labelColor : RGB(120, 116, 106), 18,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, selected ? FW_BOLD : FW_NORMAL);
    }

    COLORREF rowColor(size_t tab) const {
        if (tab == 1) return SiteSetText;
        if (tab == 3) return SiteRuneText;
        return SiteUniqueText;
    }

    COLORREF titleColor(size_t tab) const {
        if (tab == 1) return SiteSetText;
        if (tab == 3) return SiteRuneText;
        return SiteUniqueText;
    }

    std::vector<std::string> selectedLines() const {
        std::vector<std::string> lines;
        const size_t tab = model.activeTab();
        const auto row = model.selectedRow();
        if (!row) return lines;
        if (tab == 1) {
            std::set<std::string> bonuses;
            for (size_t member = 0; member < model.groupSize(tab, *row); ++member) {
                const auto* record = model.groupRecordAt(tab, *row, member);
                for (const auto& property : record->properties)
                    if (!property.text.empty() && property.text.find("set bonus:") != std::string::npos) bonuses.insert(property.text);
            }
            lines.emplace_back("SET BONUSES");
            lines.insert(lines.end(), bonuses.begin(), bonuses.end());
            for (size_t member = 0; member < model.groupSize(tab, *row); ++member) {
                const auto* record = model.groupRecordAt(tab, *row, member);
                lines.emplace_back(""); lines.push_back(record->name);
                auto memberLines = recordLines(*record, true);
                lines.insert(lines.end(), memberLines.begin(), memberLines.end());
            }
            return lines;
        }
        const auto detail = model.detailFor(tab, *row);
        return detail.lines;
    }

    int detailLineCount() const {
        const auto row = model.selectedRow();
        if (!row) return 0;
        if (model.activeTab() != 3) return static_cast<int>(selectedLines().size());
        size_t maximum = 0;
        for (size_t member = 0; member < model.groupSize(3, *row); ++member) {
            const auto* record = model.groupRecordAt(3, *row, member);
            maximum = std::max(maximum, recordLines(*record, false).size());
        }
        return static_cast<int>(maximum);
    }

    int measuredLineHeight(HDC dc, const RECT& rect, const std::string& value) const {
        RECT measured{rect.left + 8, 0, rect.right - 18, 54};
        const auto converted = wide(value);
        const HFONT selected = font(17);
        const auto old = SelectObject(dc, selected);
        DrawTextW(dc, converted.c_str(), static_cast<int>(converted.size()), &measured,
            DT_WORDBREAK | DT_CENTER | DT_NOPREFIX | DT_CALCRECT);
        SelectObject(dc, old);
        DeleteObject(selected);
        return std::max<int>(27, measured.bottom - measured.top);
    }

    ScrollMetrics currentScrollMetrics() const {
        ScrollMetrics metrics;
        if (model.activeTab() == 3 || window.load() == nullptr) return metrics;
        const auto lines = selectedLines();
        if (lines.empty()) return metrics;
        RECT client{};
        GetClientRect(window.load(), &client);
        RECT body = layoutFor(client.right, client.bottom).detail;
        body.left += 8;
        body.top += 60;
        body.right -= 8;
        const HDC dc = GetDC(window.load());
        if (dc == nullptr) return metrics;
        int totalHeight = 0;
        std::vector<int> heights;
        heights.reserve(lines.size());
        for (const auto& line : lines) {
            const int height = measuredLineHeight(dc, body, line);
            heights.push_back(height);
            totalHeight += height;
        }
        ReleaseDC(window.load(), dc);
        const int viewport = body.bottom - body.top;
        if (totalHeight <= viewport) return metrics;
        int tailHeight = 0;
        int maximum = static_cast<int>(lines.size());
        for (int index = static_cast<int>(lines.size()) - 1; index >= 0; --index) {
            if (tailHeight + heights[static_cast<size_t>(index)] > viewport && maximum < static_cast<int>(lines.size())) break;
            tailHeight += heights[static_cast<size_t>(index)];
            maximum = index;
        }
        metrics.visible = true;
        metrics.maximum = std::max(1, maximum);
        metrics.track = {body.right - 14, body.top, body.right - 4, body.bottom};
        const int trackHeight = metrics.track.bottom - metrics.track.top;
        const int thumbHeight = std::max(30, trackHeight * viewport / std::max(viewport, totalHeight));
        const int travel = std::max(1, trackHeight - thumbHeight);
        const int clampedScroll = std::clamp(detailScroll, 0, metrics.maximum);
        const int thumbY = metrics.track.top + travel * clampedScroll / metrics.maximum;
        metrics.thumb = {metrics.track.left - 3, thumbY, metrics.track.right + 3, thumbY + thumbHeight};
        return metrics;
    }

    bool beginScrollDrag(int x, int y) {
        const ScrollMetrics metrics = currentScrollMetrics();
        if (!metrics.visible) return false;
        if (contains(metrics.thumb, x, y)) {
            draggingScroll = true;
            scrollDragOffset = y - metrics.thumb.top;
            SetCapture(window.load());
            return true;
        }
        if (contains(metrics.track, x, y)) {
            const int thumbHeight = static_cast<int>(metrics.thumb.bottom - metrics.thumb.top);
            const int travel = std::max(1, static_cast<int>(metrics.track.bottom - metrics.track.top) - thumbHeight);
            const int target = std::clamp(y - static_cast<int>(metrics.track.top) - thumbHeight / 2, 0, travel);
            detailScroll = target * metrics.maximum / travel;
            InvalidateRect(window.load(), nullptr, FALSE);
            return true;
        }
        return false;
    }

    void continueScrollDrag(int y) {
        const ScrollMetrics metrics = currentScrollMetrics();
        if (!metrics.visible) return;
        const int thumbHeight = static_cast<int>(metrics.thumb.bottom - metrics.thumb.top);
        const int travel = std::max(1, static_cast<int>(metrics.track.bottom - metrics.track.top) - thumbHeight);
        const int target = std::clamp(y - scrollDragOffset - static_cast<int>(metrics.track.top), 0, travel);
        const int next = target * metrics.maximum / travel;
        if (next != detailScroll) {
            detailScroll = next;
            InvalidateRect(window.load(), nullptr, FALSE);
        }
    }

    COLORREF detailLineColor(size_t tab, const std::string& value,
                             const std::set<std::string>* itemNames) const {
        if (tab == 1 && itemNames != nullptr && itemNames->contains(value)) return SiteSetText;
        const std::string normalized = lower(value);
        if (tab == 1 && (value == "SET BONUSES" || normalized.find("set bonus:") != std::string::npos))
            return SiteSetText;
        if (normalized.starts_with("rarity:")) return SiteRarityText;
        if (normalized.starts_with("required ") || normalized.ends_with(" only)"))
            return SiteRequirementText;
        if (tab == 2 && normalized.starts_with("runes:")) return SiteRuneText;
        if (tab == 3 && (normalized.starts_with("sockets:") || normalized.starts_with("maximum sockets:")))
            return SiteSocketText;
        static constexpr std::array<const char*, 14> basePrefixes{
            "base:", "set:", "runes:", "item type:", "category:", "tier:", "class:",
            "weapon type:", "damage:", "average damage:", "defense:", "sockets:",
            "maximum sockets:", "origin:"
        };
        if (std::any_of(basePrefixes.begin(), basePrefixes.end(), [&](const char* prefix) {
            return normalized.starts_with(prefix);
        })) return SiteBaseText;
        if (value.empty()) return ParchmentText;
        if (tab == 3) return SiteBaseText;
        return SiteMagicText;
    }

    int drawLines(HDC dc, RECT rect, const std::vector<std::string>& lines, int scroll, bool centered = true,
        const std::set<std::string>* accentLines = nullptr, COLORREF accentColor = ParchmentText) const {
        const int lineHeight = 27;
        int y = rect.top;
        const int first = std::min<int>(scroll, static_cast<int>(lines.size()));
        for (int index = first; index < static_cast<int>(lines.size()) && y < rect.bottom; ++index) {
            RECT line{rect.left + 8, y, rect.right - 18, std::min<LONG>(y + lineHeight * 2, rect.bottom)};
            const UINT flags = DT_WORDBREAK | (centered ? DT_CENTER : DT_LEFT) | DT_NOPREFIX;
            RECT measured = line;
            const auto converted = wide(lines[static_cast<size_t>(index)]);
            const HFONT selected = font(17);
            const auto old = SelectObject(dc, selected);
            DrawTextW(dc, converted.c_str(), static_cast<int>(converted.size()), &measured, flags | DT_CALCRECT);
            SelectObject(dc, old); DeleteObject(selected);
            const int height = std::max<int>(lineHeight, measured.bottom - measured.top);
            line.bottom = std::min<LONG>(y + height, rect.bottom);
            const auto& value = lines[static_cast<size_t>(index)];
            COLORREF color = accentLines != nullptr && accentLines->contains(value) ? accentColor : ParchmentText;
            color = detailLineColor(model.activeTab(), value, accentLines);
            text(dc, value, line, color, 17, flags);
            y += height;
        }
        return static_cast<int>(lines.size());
    }

    void paint(HWND hwnd) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hwnd, &paint);
        RECT client{}; GetClientRect(hwnd, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        const auto oldBitmap = SelectObject(dc, bitmap);
        const HBRUSH background = CreateSolidBrush(RGB(24, 21, 14));
        FillRect(dc, &client, background); DeleteObject(background);
        const Layout layout = layoutFor(client.right, client.bottom);

        RECT title{80, 12, client.right - 80, 64};
        text(dc, "D2R REIMAGINED ITEM DATABASE", title, RGB(221, 197, 137), 30,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE, FW_BOLD);
        button(dc, layout.close, "X", false);
        static constexpr std::array<const char*, 4> labels{"UNIQUES", "SETS", "RUNEWORDS", "BASES"};
        for (size_t tab = 0; tab < labels.size(); ++tab) button(dc, layout.tabs[tab], labels[tab], model.activeTab() == tab);

        const size_t tab = model.activeTab();
        const size_t page = model.page(tab);
        const size_t first = page * model.pageSize() + 1;
        const size_t last = page * model.pageSize() + model.visibleCount(tab);
        RECT countRect{40, 246, layout.detail.left - 15, 276};
        const std::string countText = model.count(tab) == 0 ? "0 RESULTS" :
            std::to_string(model.count(tab)) + " RESULTS - SHOWING " + std::to_string(first) + "-" + std::to_string(last);
        text(dc, countText,
            countRect, RGB(238, 233, 217), 17, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (size_t row = 0; row < PrototypePageSize; ++row) {
            if (row < model.visibleCount(tab))
                button(dc, layout.rows[row], model.labelAt(tab, row), model.selectedRow(tab) == row, true, rowColor(tab));
        }
        button(dc, layout.previous, "PREVIOUS", false, page > 0);
        button(dc, layout.next, "NEXT", false, page + 1 < model.pageCount(tab));
        RECT pageRect{layout.previous.left, layout.previous.bottom + 2, layout.next.right, layout.previous.bottom + 28};
        const size_t pages = model.pageCount(tab);
        text(dc, "PAGE " + std::to_string(pages == 0 ? 0 : page + 1) + " OF " + std::to_string(pages), pageRect,
            RGB(221, 197, 137), 15, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const auto selected = model.selectedRow();
        if (selected) {
            RECT detail = layout.detail;
            const auto* record = model.selectedRecord();
            const std::string titleText = tab == 1 || tab == 3 ? model.labelAt(tab, *selected) : record->name;
            RECT detailTitle{detail.left, detail.top, detail.right, detail.top + 55};
            text(dc, titleText, detailTitle, titleColor(tab), 27,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, FW_BOLD);
            RECT body{detail.left + 8, detail.top + 60, detail.right - 8, detail.bottom};
            if (tab == 3) {
                const size_t members = model.groupSize(tab, *selected);
                const int gap = 18;
                const int columnWidth = (body.right - body.left - gap * static_cast<int>(members > 0 ? members - 1 : 0)) / std::max<int>(1, static_cast<int>(members));
                for (size_t member = 0; member < members; ++member) {
                    const auto* base = model.groupRecordAt(tab, *selected, member);
                    RECT column{body.left + static_cast<int>(member) * (columnWidth + gap), body.top,
                        body.left + static_cast<int>(member) * (columnWidth + gap) + columnWidth, body.bottom};
                    RECT baseTitle{column.left, column.top, column.right, column.top + 42};
                    text(dc, base->name, baseTitle, SiteUniqueText, 21,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, FW_BOLD);
                    column.top += 48;
                    drawLines(dc, column, recordLines(*base, false), detailScroll, true);
                }
            } else {
                const auto lines = selectedLines();
                std::set<std::string> setItemNames;
                if (tab == 1) {
                    for (size_t member = 0; member < model.groupSize(tab, *selected); ++member)
                        setItemNames.insert(model.groupRecordAt(tab, *selected, member)->name);
                }
                drawLines(dc, body, lines, detailScroll, true,
                    tab == 1 ? &setItemNames : nullptr, SiteSetText);
                const ScrollMetrics metrics = currentScrollMetrics();
                detailScroll = std::clamp(detailScroll, 0, metrics.maximum);
                if (metrics.visible) {
                    const RECT track = metrics.track;
                    const HBRUSH trackBrush = CreateSolidBrush(RGB(65, 57, 39)); FillRect(dc, &track, trackBrush); DeleteObject(trackBrush);
                    const HBRUSH thumbBrush = CreateSolidBrush(RGB(178, 132, 62)); FillRect(dc, &metrics.thumb, thumbBrush); DeleteObject(thumbBrush);
                }
            }
        }
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
        EndPaint(hwnd, &paint);
    }

    void click(int x, int y) {
        RECT client{}; GetClientRect(window.load(), &client);
        const Layout layout = layoutFor(client.right, client.bottom);
        if (contains(layout.close, x, y)) { hideOverlay(); return; }
        for (size_t tab = 0; tab < layout.tabs.size(); ++tab) {
            if (contains(layout.tabs[tab], x, y) && model.switchTab(tab)) {
                detailScroll = 0; syncControls(); InvalidateRect(window.load(), nullptr, FALSE); return;
            }
        }
        for (size_t row = 0; row < model.visibleCount(model.activeTab()); ++row) {
            if (contains(layout.rows[row], x, y) && model.select(row)) {
                detailScroll = 0; InvalidateRect(window.load(), nullptr, FALSE); return;
            }
        }
        if (contains(layout.previous, x, y) && model.previousPage()) {
            detailScroll = 0; InvalidateRect(window.load(), nullptr, FALSE); return;
        }
        if (contains(layout.next, x, y) && model.nextPage()) {
            detailScroll = 0; InvalidateRect(window.load(), nullptr, FALSE);
        }
    }
};

OverlayHost::OverlayHost(PrototypeViewModel& model, const D2RL::PluginContext* plugin)
    : impl_(std::make_unique<Impl>(model, plugin)) {}
OverlayHost::~OverlayHost() { stop(); }
bool OverlayHost::start() { return impl_->start(); }
void OverlayHost::toggle() noexcept { impl_->toggle(); }
void OverlayHost::stop() noexcept { if (impl_) impl_->stop(); }
}
