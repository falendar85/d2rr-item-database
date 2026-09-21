#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <D2RLPlugin/api.h>
#include <itemdb/overlay.hpp>
#include <itemdb/prototype.hpp>
#include <algorithm>
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
constexpr UINT ToggleMessage = WM_APP + 0x241;
constexpr UINT StopMessage = WM_APP + 0x242;
constexpr UINT TrackingTimer = 1;
constexpr COLORREF ParchmentText = RGB(238, 233, 217);
constexpr COLORREF SiteUniqueText = RGB(199, 183, 144);
constexpr COLORREF SiteSetText = RGB(76, 194, 56);

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

Layout layoutFor(int width, int height) {
    Layout layout;
    layout.close = {width - 58, 16, width - 18, 56};
    const int tabLeft = 32, tabRight = width - 32, tabTop = 78, tabHeight = 58;
    const int tabWidth = (tabRight - tabLeft) / 4;
    for (int tab = 0; tab < 4; ++tab)
        layout.tabs[static_cast<size_t>(tab)] = {tabLeft + tab * tabWidth, tabTop, tabLeft + (tab + 1) * tabWidth, tabTop + tabHeight};
    const int listLeft = 38;
    const int listWidth = std::clamp(width * 27 / 100, 300, 430);
    const int rowsTop = 190;
    const int navigationHeight = 92;
    const int available = std::max(360, height - rowsTop - navigationHeight - 30);
    const int rowHeight = std::clamp(available / static_cast<int>(PrototypePageSize), 42, 62);
    for (int row = 0; row < static_cast<int>(PrototypePageSize); ++row)
        layout.rows[static_cast<size_t>(row)] = {listLeft, rowsTop + row * rowHeight, listLeft + listWidth, rowsTop + (row + 1) * rowHeight - 4};
    const int navTop = rowsTop + static_cast<int>(PrototypePageSize) * rowHeight + 4;
    layout.previous = {listLeft, navTop, listLeft + listWidth / 2 - 3, navTop + 46};
    layout.next = {listLeft + listWidth / 2 + 3, navTop, listLeft + listWidth, navTop + 46};
    layout.detail = {listLeft + listWidth + 28, 170, width - 40, height - 35};
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
    HWND gameWindow = nullptr;

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
        gameWindow = findGameWindow(nullptr);
        const HWND handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, OverlayClass, L"D2RR Item Database",
            WS_POPUP, 100, 100, 1200, 760, gameWindow, nullptr, instance, this);
        window = handle;
        SetEvent(ready);
        if (handle == nullptr) return;
        SetTimer(handle, TrackingTimer, 100, nullptr);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        window = nullptr;
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
            if (self->visible) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); InvalidateRect(hwnd, nullptr, FALSE); }
            else ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case StopMessage:
            DestroyWindow(hwnd);
            return 0;
        case WM_TIMER:
            self->updatePlacement(false);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->paint(hwnd);
            return 0;
        case WM_LBUTTONUP:
            self->click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            RECT client{}; GetClientRect(hwnd, &client);
            const RECT detail = layoutFor(client.right, client.bottom).detail;
            if (contains(detail, point.x, point.y)) {
                const int visibleLines = std::max<int>(1, (detail.bottom - detail.top - 70) / 27);
                const int maximum = std::max(0, self->detailLineCount() - visibleLines);
                self->detailScroll = std::clamp(self->detailScroll + (GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -3 : 3), 0, maximum);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE || wparam == VK_F8) { self->visible = false; ShowWindow(hwnd, SW_HIDE); return 0; }
            if (wparam == VK_LEFT && self->model.previousPage()) { self->detailScroll = 0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wparam == VK_RIGHT && self->model.nextPage()) { self->detailScroll = 0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, TrackingTimer);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
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
        SetWindowLongPtrW(window.load(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
        SetWindowPos(window.load(), HWND_TOPMOST, x, y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
        if (tab == 3) return ParchmentText; // Base-family headings are parchment on the site.
        return SiteUniqueText;
    }

    COLORREF titleColor(size_t tab) const {
        if (tab == 1) return SiteSetText;
        if (tab == 3) return ParchmentText;
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
            const COLORREF color = accentLines != nullptr && accentLines->contains(value) ? accentColor : ParchmentText;
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
        RECT countRect{40, 145, layout.detail.left - 15, 183};
        text(dc, std::to_string(model.count(tab)) + " RESULTS - SHOWING " + std::to_string(first) + "-" + std::to_string(last),
            countRect, RGB(238, 233, 217), 17, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (size_t row = 0; row < PrototypePageSize; ++row) {
            if (row < model.visibleCount(tab))
                button(dc, layout.rows[row], model.labelAt(tab, row), model.selectedRow(tab) == row, true, rowColor(tab));
        }
        button(dc, layout.previous, "PREVIOUS", false, page > 0);
        button(dc, layout.next, "NEXT", false, page + 1 < model.pageCount(tab));
        RECT pageRect{layout.previous.left, layout.previous.bottom + 2, layout.next.right, layout.previous.bottom + 28};
        text(dc, "PAGE " + std::to_string(page + 1) + " OF " + std::to_string(model.pageCount(tab)), pageRect,
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
                const int total = drawLines(dc, body, lines, detailScroll, true,
                    tab == 1 ? &setItemNames : nullptr, SiteSetText);
                if (total > 18) {
                    RECT track{body.right - 8, body.top, body.right - 2, body.bottom};
                    const HBRUSH trackBrush = CreateSolidBrush(RGB(65, 57, 39)); FillRect(dc, &track, trackBrush); DeleteObject(trackBrush);
                    const int range = std::max(1, total - 1);
                    const int thumbHeight = std::max<int>(30, (track.bottom - track.top) * 14 / std::max(14, total));
                    const int thumbY = track.top + (track.bottom - track.top - thumbHeight) * std::min(detailScroll, range) / range;
                    RECT thumb{track.left - 2, thumbY, track.right + 2, thumbY + thumbHeight};
                    const HBRUSH thumbBrush = CreateSolidBrush(RGB(178, 132, 62)); FillRect(dc, &thumb, thumbBrush); DeleteObject(thumbBrush);
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
        if (contains(layout.close, x, y)) { visible = false; ShowWindow(window.load(), SW_HIDE); return; }
        for (size_t tab = 0; tab < layout.tabs.size(); ++tab) {
            if (contains(layout.tabs[tab], x, y) && model.switchTab(tab)) {
                detailScroll = 0; InvalidateRect(window.load(), nullptr, FALSE); return;
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
