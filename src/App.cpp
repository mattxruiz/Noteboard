#include "App.h"
#include "MediaViewer.h"

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <random>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "mfplat.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {

const Color C_BG(255, 18, 22, 20);
const Color C_RAIL(255, 22, 27, 24);
const Color C_ELEV(255, 32, 39, 35);
const Color C_SOFT(255, 42, 52, 46);
const Color C_SOFT2(255, 52, 64, 56);
const Color C_INK(255, 236, 240, 234);
const Color C_MUTED(255, 148, 162, 150);
const Color C_ACCENT(255, 196, 232, 74);
const Color C_ACCENT_DIM(255, 140, 170, 40);
const Color C_DANGER(255, 232, 106, 90);
const Color C_LINE(40, 236, 240, 234);
const Color C_CARD_HOVER(255, 38, 48, 42);

constexpr int HIT_NONE = -1;
constexpr int HIT_NEW = 1;
constexpr int HIT_EDIT_PROJ = 2;
constexpr int HIT_DEL_PROJ = 3;
constexpr int HIT_ADD_TEXT = 4;
constexpr int HIT_ADD_IMG = 5;
constexpr int HIT_ADD_FILE = 6;
constexpr int HIT_DEL_ENTRY = 7;
constexpr int HIT_PROJ_BASE = 1000;
constexpr int HIT_CARD_BASE = 5000;

constexpr int CARD_W = 200;
constexpr int CARD_H = 236;
constexpr int CARD_GAP = 14;
constexpr int PROJ_ROW_H = 64;
constexpr int ID_EDIT_NAME = 1003;

ULONG_PTR g_gdiplusToken = 0;

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string MakeId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << NowMs() << "-" << dist(rng);
    return ss.str();
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().wstring();
}

std::wstring FormatTime(std::int64_t ms) {
    if (ms <= 0) return L"";
    time_t sec = (time_t)(ms / 1000);
    struct tm t {};
    localtime_s(&t, &sec);
    wchar_t buf[64];
    wcsftime(buf, 64, L"%b %d · %Y", &t);
    return buf;
}

bool IsImageExt(const std::wstring& path) {
    return IsImageFile(path);
}

std::wstring FileExtLabel(const std::wstring& path) {
    auto e = fs::path(path).extension().wstring();
    if (e.empty()) return L"FILE";
    if (e[0] == L'.') e.erase(0, 1);
    for (auto& c : e) c = (wchar_t)towupper(c);
    if (e.size() > 5) e.resize(5);
    return e;
}

void RoundRectPath(GraphicsPath& path, RectF r, REAL radius) {
    if (r.Width < 1.f || r.Height < 1.f) {
        path.AddRectangle(r);
        return;
    }
    radius = (std::min)(radius, (std::min)(r.Width, r.Height) * 0.5f);
    REAL d = radius * 2.f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRound(Graphics& g, const RectF& r, REAL radius, const Color& c) {
    if (r.Width < 1.f || r.Height < 1.f) return;
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    SolidBrush br(c);
    g.FillPath(&br, &path);
}

void StrokeRound(Graphics& g, const RectF& r, REAL radius, const Color& c, REAL width = 1.0f) {
    if (r.Width < 1.f || r.Height < 1.f) return;
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    Pen pen(c, width);
    g.DrawPath(&pen, &path);
}

bool PtIn(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

RectF ToRF(const RECT& r) {
    return RectF((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}

void DrawLabel(Graphics& g, const wchar_t* text, const RectF& r, const Color& c,
               REAL size, FontStyle style = FontStyleRegular,
               StringAlignment align = StringAlignmentNear,
               StringAlignment line = StringAlignmentCenter) {
    if (!text || r.Width < 1.f || r.Height < 1.f) return;
    FontFamily ff(L"Segoe UI");
    Font font(&ff, size, style, UnitPixel);
    SolidBrush br(c);
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(line);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(text, -1, &font, r, &fmt, &br);
}

void DrawLabelWrap(Graphics& g, const wchar_t* text, const RectF& r, const Color& c, REAL size) {
    if (!text || r.Width < 1.f || r.Height < 1.f) return;
    FontFamily ff(L"Segoe UI");
    Font font(&ff, size, FontStyleRegular, UnitPixel);
    SolidBrush br(c);
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisWord);
    g.DrawString(text, -1, &font, r, &fmt, &br);
}

void DrawButton(Graphics& g, const RECT& rc, const wchar_t* label, bool primary, bool danger,
                bool hover, bool pressed) {
    RectF r = ToRF(rc);
    if (r.Width < 1.f || r.Height < 1.f) return;
    Color fill = primary ? C_ACCENT : (danger ? Color(40, 232, 106, 90) : C_SOFT);
    if (hover && !pressed) {
        if (primary) fill = Color(255, 212, 242, 100);
        else if (danger) fill = Color(70, 232, 106, 90);
        else fill = C_SOFT2;
    }
    if (pressed) fill = primary ? C_ACCENT_DIM : C_ELEV;
    FillRound(g, r, 10, fill);
    if (!primary && !danger) StrokeRound(g, r, 10, C_LINE);
    Color ink = primary ? Color(255, 18, 22, 20) : (danger ? C_DANGER : C_INK);
    DrawLabel(g, label, r, ink, 13.0f, FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
}

Bitmap* MakeTextThumbBmp(const std::wstring& text, int w, int h) {
    auto* bmp = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(255, 40, 50, 44));
    SolidBrush bar(C_ACCENT);
    g.FillRectangle(&bar, 0, 0, 5, h);
    DrawLabelWrap(g, text.c_str(), RectF(14, 14, (REAL)w - 28, (REAL)h - 28), C_MUTED, 13);
    return bmp;
}

Bitmap* MakeFileThumbBmp(const std::wstring& path, int w, int h) {
    auto* bmp = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(255, 36, 44, 40));
    FillRound(g, RectF((REAL)w * 0.28f, (REAL)h * 0.18f, (REAL)w * 0.44f, (REAL)h * 0.52f), 10, C_SOFT2);
    auto ext = FileExtLabel(path);
    DrawLabel(g, ext.c_str(), RectF(0, (REAL)h * 0.28f, (REAL)w, 36), C_ACCENT, 18, FontStyleBold,
        StringAlignmentCenter, StringAlignmentCenter);
    auto name = fs::path(path).filename().wstring();
    DrawLabel(g, name.c_str(), RectF(12, (REAL)h * 0.72f, (REAL)w - 24, 28), C_MUTED, 11,
        FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
    return bmp;
}

Bitmap* MakeImageThumbBmp(const std::wstring& path, int w, int h) {
    std::unique_ptr<Bitmap> src(Bitmap::FromFile(path.c_str()));
    if (!src || src->GetLastStatus() != Ok || src->GetWidth() == 0 || src->GetHeight() == 0)
        return MakeFileThumbBmp(path, w, h);

    auto* bmp = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(255, 28, 34, 30));

    REAL scale = (std::max)((REAL)w / (REAL)src->GetWidth(), (REAL)h / (REAL)src->GetHeight());
    REAL dw = (REAL)src->GetWidth() * scale;
    REAL dh = (REAL)src->GetHeight() * scale;
    g.DrawImage(src.get(), ((REAL)w - dw) * 0.5f, ((REAL)h - dh) * 0.5f, dw, dh);
    return bmp;
}

Bitmap* MakeLetterThumbBmp(wchar_t letter, int w, int h) {
    auto* bmp = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.Clear(Color(255, 48, 60, 52));
    wchar_t s[2] = { letter, 0 };
    FontFamily ff(L"Georgia");
    Font font(&ff, (REAL)(h * 0.42), FontStyleRegular, UnitPixel);
    SolidBrush br(C_ACCENT);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(s, 1, &font, RectF(0, 0, (REAL)w, (REAL)h), &fmt, &br);
    return bmp;
}

void BlitBitmap(HDC hdc, Bitmap& back, int w, int h) {
    if (w <= 0 || h <= 0) return;
    HBITMAP hb = nullptr;
    if (back.GetHBITMAP(Color(0, 0, 0, 0), &hb) != Ok || !hb) return;
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, hb);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(hb);
    DeleteDC(mem);
}

// ——— Modal ———
struct ModalBtn {
    RECT rc{};
    const wchar_t* label = L"";
    int id = 0;
    bool primary = false;
    bool danger = false;
};

struct ModalState {
    std::wstring caption;
    std::wstring title;
    std::wstring body;
    std::wstring description; // project description
    std::wstring filePath;
    EntryType type = EntryType::Text;
    bool isNewProject = false;
    bool isEditProject = false;
    bool deleted = false;
    bool ok = false;
    bool closed = false;
    int hover = 0;
    int press = 0;
    HWND editTitle = nullptr;
    HWND editBody = nullptr;
    Bitmap* preview = nullptr;
    std::vector<ModalBtn> buttons;
    RECT rcPreview{};
    int width = 0;
    int height = 0;
};

bool RunModernModal(HWND owner, HINSTANCE inst, ModalState& st, const wchar_t* caption, int w, int h) {
    st.caption = caption ? caption : L"";
    st.width = w;
    st.height = h;
    st.closed = false;
    st.ok = false;

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* st = reinterpret_cast<ModalState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg) {
            case WM_CREATE: {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                st = reinterpret_cast<ModalState*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

                HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                    0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                HFONT fontTitle = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                    0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

                if (st->isNewProject || st->isEditProject) {
                    st->editTitle = CreateWindowExW(0, L"EDIT", st->title.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        28, 78, st->width - 56, 34, hwnd, (HMENU)1, cs->hInstance, nullptr);
                    SendMessageW(st->editTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);
                    st->editBody = CreateWindowExW(0, L"EDIT", st->description.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                        28, 150, st->width - 56, 90, hwnd, (HMENU)2, cs->hInstance, nullptr);
                    SendMessageW(st->editBody, WM_SETFONT, (WPARAM)font, TRUE);
                    SetFocus(st->editTitle);
                } else {
                    st->editTitle = CreateWindowExW(0, L"EDIT", st->title.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        28, 56, st->width - 56, 34, hwnd, (HMENU)1, cs->hInstance, nullptr);
                    SendMessageW(st->editTitle, WM_SETFONT, (WPARAM)fontTitle, TRUE);

                    if (st->type == EntryType::Text) {
                        st->editBody = CreateWindowExW(0, L"EDIT", st->body.c_str(),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                            28, 110, st->width - 56, st->height - 200, hwnd, (HMENU)2, cs->hInstance, nullptr);
                    } else {
                        st->rcPreview = { 28, 100, st->width - 28, 100 + 220 };
                        st->editBody = CreateWindowExW(0, L"EDIT", st->body.c_str(),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                            28, 350, st->width - 56, 80, hwnd, (HMENU)2, cs->hInstance, nullptr);
                        if (!st->filePath.empty() && IsImageExt(st->filePath)) {
                            st->preview = Bitmap::FromFile(st->filePath.c_str());
                            if (st->preview && (st->preview->GetLastStatus() != Ok
                                || st->preview->GetWidth() == 0)) {
                                delete st->preview;
                                st->preview = nullptr;
                            }
                        }
                    }
                    if (st->editBody) SendMessageW(st->editBody, WM_SETFONT, (WPARAM)font, TRUE);
                }
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                if (!st) break;
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc; GetClientRect(hwnd, &rc);
                if (rc.right > 0 && rc.bottom > 0) {
                    Bitmap back(rc.right, rc.bottom, PixelFormat32bppARGB);
                    Graphics g(&back);
                    g.SetSmoothingMode(SmoothingModeAntiAlias);
                    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
                    g.Clear(C_ELEV);
                    SolidBrush accent(C_ACCENT);
                    g.FillRectangle(&accent, 0, 0, rc.right, 3);

                    if (st->isNewProject || st->isEditProject) {
                        DrawLabel(g, st->isNewProject ? L"New project" : L"Edit project",
                            RectF(28, 22, (REAL)rc.right - 56, 28), C_INK, 22, FontStyleBold);
                        DrawLabel(g, L"Name", RectF(28, 56, 120, 18), C_MUTED, 12);
                        FillRound(g, RectF(24, 74, (REAL)rc.right - 48, 42), 10, C_BG);
                        DrawLabel(g, L"Description", RectF(28, 128, 160, 18), C_MUTED, 12);
                        FillRound(g, RectF(24, 146, (REAL)rc.right - 48, 98), 10, C_BG);
                    } else {
                        DrawLabel(g, st->caption.c_str(), RectF(28, 16, 240, 22), C_MUTED, 12, FontStyleBold);
                        FillRound(g, RectF(24, 50, (REAL)rc.right - 48, 42), 10, C_BG);
                        if (st->type == EntryType::Text) {
                            FillRound(g, RectF(24, 104, (REAL)rc.right - 48, (REAL)rc.bottom - 184), 12, C_BG);
                        } else {
                            FillRound(g, ToRF(st->rcPreview), 12, C_BG);
                            if (st->preview) {
                                UINT pw0 = st->preview->GetWidth(), ph0 = st->preview->GetHeight();
                                if (pw0 > 0 && ph0 > 0) {
                                    REAL pw = (REAL)(st->rcPreview.right - st->rcPreview.left - 16);
                                    REAL ph = (REAL)(st->rcPreview.bottom - st->rcPreview.top - 16);
                                    REAL scale = (std::min)(pw / (REAL)pw0, ph / (REAL)ph0);
                                    REAL dw = (REAL)pw0 * scale, dh = (REAL)ph0 * scale;
                                    g.DrawImage(st->preview,
                                        st->rcPreview.left + 8 + (pw - dw) * 0.5f,
                                        st->rcPreview.top + 8 + (ph - dh) * 0.5f, dw, dh);
                                }
                            } else if (st->type == EntryType::File || st->type == EntryType::Image) {
                                const wchar_t* hint = L"No preview — use View / Play";
                                if (!st->filePath.empty()) {
                                    if (IsVideoExt(st->filePath)) hint = L"Video file — click Play to watch in app";
                                    else if (!IsImageExt(st->filePath)) hint = L"File attached — use Open or Show in folder";
                                }
                                DrawLabel(g, hint, ToRF(st->rcPreview), C_MUTED, 14,
                                    FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
                            }
                            FillRound(g, RectF(24, 344, (REAL)rc.right - 48, 92), 12, C_BG);
                            DrawLabel(g, L"Notes", RectF(28, 328, 100, 16), C_MUTED, 11);
                        }
                    }

                    for (auto& b : st->buttons)
                        DrawButton(g, b.rc, b.label, b.primary, b.danger, st->hover == b.id, st->press == b.id);

                    BlitBitmap(hdc, back, rc.right, rc.bottom);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(236, 240, 234));
                SetBkColor(hdc, RGB(18, 22, 20));
                static HBRUSH br = CreateSolidBrush(RGB(18, 22, 20));
                return (LRESULT)br;
            }
            case WM_MOUSEMOVE: {
                if (!st) return 0;
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                int hit = 0;
                for (auto& b : st->buttons) if (PtIn(b.rc, x, y)) hit = b.id;
                if (hit != st->hover) { st->hover = hit; InvalidateRect(hwnd, nullptr, FALSE); }
                return 0;
            }
            case WM_LBUTTONDOWN: {
                if (!st) return 0;
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                for (auto& b : st->buttons) if (PtIn(b.rc, x, y)) {
                    st->press = b.id;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                if (!st) return 0;
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                int pressed = st->press;
                st->press = 0;
                ReleaseCapture();

                auto readFields = [&]() {
                    wchar_t tbuf[512]{};
                    if (st->editTitle) GetWindowTextW(st->editTitle, tbuf, 512);
                    st->title = tbuf;
                    if (st->editBody) {
                        int len = GetWindowTextLengthW(st->editBody) + 1;
                        std::wstring body(len, L'\0');
                        GetWindowTextW(st->editBody, body.data(), len);
                        if (!body.empty() && body.back() == L'\0') body.pop_back();
                        if (st->isNewProject || st->isEditProject) st->description = body;
                        else st->body = body;
                    }
                };

                for (auto& b : st->buttons) {
                    if (b.id != pressed || !PtIn(b.rc, x, y)) continue;
                    if (b.id == IDOK) {
                        readFields();
                        st->ok = true;
                        DestroyWindow(hwnd);
                    } else if (b.id == IDCANCEL) {
                        st->ok = false;
                        DestroyWindow(hwnd);
                    } else if (b.id == 10) {
                        if (MessageBoxW(hwnd, L"Delete this entry?", L"Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            st->deleted = true;
                            st->ok = true;
                            DestroyWindow(hwnd);
                        }
                    } else if (b.id == 11) { // replace
                        wchar_t file[MAX_PATH] = L"";
                        OPENFILENAMEW ofn{ sizeof(ofn) };
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = st->type == EntryType::Image
                            ? L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp\0All\0*.*\0"
                            : L"All files\0*.*\0";
                        ofn.lpstrFile = file;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
                        if (GetOpenFileNameW(&ofn)) {
                            st->filePath = file;
                            delete st->preview;
                            st->preview = nullptr;
                            if (IsImageExt(file)) {
                                st->preview = Bitmap::FromFile(file);
                                if (st->preview && (st->preview->GetLastStatus() != Ok || st->preview->GetWidth() == 0)) {
                                    delete st->preview;
                                    st->preview = nullptr;
                                }
                                if (st->type == EntryType::File) st->type = EntryType::Image;
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    } else if (b.id == 12) { // open file with default app
                        if (!st->filePath.empty())
                            ShellExecuteW(hwnd, L"open", st->filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    } else if (b.id == 13) { // show in folder
                        ShowInFolder(hwnd, st->filePath);
                    } else if (b.id == 14) { // view / play in app
                        if (!st->filePath.empty()) {
                            std::wstring t = st->title.empty()
                                ? fs::path(st->filePath).filename().wstring()
                                : st->title;
                            ShowMediaViewer(hwnd, GetModuleHandleW(nullptr), st->filePath, t);
                        }
                    }
                    break;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE && st) { st->ok = false; DestroyWindow(hwnd); }
                return 0;
            case WM_CLOSE:
                if (st) st->ok = false;
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                if (st) {
                    delete st->preview;
                    st->preview = nullptr;
                    st->closed = true; // NO PostQuitMessage — that killed the main app
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        };
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"ProjectBoardModal";
        RegisterClassExW(&wc);
        reg = true;
    }

    st.buttons.clear();
    int by = h - 64;
    if (st.isNewProject || st.isEditProject) {
        st.buttons.push_back({ { w - 210, by, w - 120, by + 36 }, L"Cancel", IDCANCEL, false, false });
        st.buttons.push_back({ { w - 108, by, w - 28, by + 36 }, st.isNewProject ? L"Create" : L"Save", IDOK, true, false });
    } else {
        st.buttons.push_back({ { 28, by, 100, by + 36 }, L"Delete", 10, false, true });
        int bx = 136;
        if (st.type == EntryType::Image || st.type == EntryType::File) {
            bool media = IsImageExt(st.filePath) || IsVideoExt(st.filePath);
            if (media) {
                st.buttons.push_back({ { bx, by, bx + 100, by + 36 },
                    IsVideoExt(st.filePath) ? L"Play" : L"View", 14, true, false });
                bx += 108;
            }
            st.buttons.push_back({ { bx, by, bx + 130, by + 36 }, L"Show in folder", 13, false, false });
            bx += 138;
            st.buttons.push_back({ { bx, by, bx + 90, by + 36 }, L"Replace", 11, false, false });
            bx += 98;
            st.buttons.push_back({ { bx, by, bx + 80, by + 36 }, L"Open", 12, false, false });
        }
        st.buttons.push_back({ { w - 210, by, w - 120, by + 36 }, L"Cancel", IDCANCEL, false, false });
        st.buttons.push_back({ { w - 108, by, w - 28, by + 36 }, L"Done", IDOK, true, false });
    }

    HWND dlg = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"ProjectBoardModal", caption,
        WS_POPUP | WS_VISIBLE,
        0, 0, w, h, owner, nullptr, inst, &st);
    if (!dlg) return false;

    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, 18, 18);
    SetWindowRgn(dlg, rgn, TRUE);

    RECT orc{};
    GetWindowRect(owner, &orc);
    SetWindowPos(dlg, HWND_TOP,
        orc.left + ((orc.right - orc.left) - w) / 2,
        orc.top + ((orc.bottom - orc.top) - h) / 2,
        0, 0, SWP_NOSIZE);

    EnableWindow(owner, FALSE);

    MSG msg{};
    while (!st.closed && IsWindow(dlg)) {
        BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
        if (ret == 0) {
            // Real app quit — re-post so main loop sees it
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (ret == -1) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);
    return st.ok;
}

} // namespace

App::App(HINSTANCE hInst)
    : hInst_(hInst)
    , storage_((fs::path(ExeDir()) / L"data").wstring()) {}

int App::run(int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    GdiplusStartupInput in;
    GdiplusStartup(&g_gdiplusToken, &in, nullptr);
    storage_.ensureDirs();
    try {
        storage_.load(projects_);
    } catch (...) {
        projects_.clear();
    }
    if (!createWindow(nCmdShow)) {
        MFShutdown();
        CoUninitialize();
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    clearThumbCache();
    GdiplusShutdown(g_gdiplusToken);
    MFShutdown();
    CoUninitialize();
    return (int)msg.wParam;
}

bool App::createWindow(int nCmdShow) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = App::WndProc;
    wc.hInstance = hInst_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"ProjectBoardMain";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, L"ProjectBoardMain", L"Project Board",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1240, 800,
        nullptr, nullptr, hInst_, this);
    if (!hwnd_) return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

void App::createChildren() {
    hwndName_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd_, (HMENU)(INT_PTR)ID_EDIT_NAME, hInst_, nullptr);
    HFONT font = CreateFontW(-26, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Georgia");
    SendMessageW(hwndName_, WM_SETFONT, (WPARAM)font, TRUE);
    oldEditProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwndName_, GWLP_WNDPROC, (LONG_PTR)App::EditProc));
    SetWindowLongPtrW(hwndName_, GWLP_USERDATA, (LONG_PTR)oldEditProc_);
}

LRESULT CALLBACK App::EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WNDPROC old = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        HWND parent = GetParent(h);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(ID_EDIT_NAME, EN_KILLFOCUS), (LPARAM)h);
        return 0;
    }
    return CallWindowProcW(old, h, m, w, l);
}

void App::layout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (rc.right < 100 || rc.bottom < 100) return;

    railW_ = 300;
    rcRail_ = { 0, 0, railW_, rc.bottom };
    rcStage_ = { railW_, 0, rc.right, rc.bottom };

    rcBtnNew_ = { 20, 88, railW_ - 20, 128 };

    const int sx = railW_ + 28;
    const int sw = rc.right - sx - 28;
    rcName_ = { sx, 24, sx + (std::max)(100, sw - 280), 62 };
    rcBtnEditProj_ = { rc.right - 28 - 250, 30, rc.right - 28 - 130, 66 };
    rcBtnDelProj_ = { rc.right - 28 - 120, 30, rc.right - 28, 66 };

    int bx = sx, by = 84;
    auto place = [&](RECT& r, int width) {
        r = { bx, by, bx + width, by + 34 };
        bx += width + 8;
    };
    place(rcBtnAddText_, 96);
    place(rcBtnAddImg_, 104);
    place(rcBtnAddFile_, 96);
    place(rcBtnDelEntry_, 118);

    rcCards_ = { sx, 140, rc.right - 28, rc.bottom - 20 };

    if (hwndName_) {
        if (activeProject()) {
            ShowWindow(hwndName_, SW_SHOW);
            MoveWindow(hwndName_, rcName_.left + 8, rcName_.top + 8,
                (rcName_.right - rcName_.left) - 16, (rcName_.bottom - rcName_.top) - 14, TRUE);
        } else {
            ShowWindow(hwndName_, SW_HIDE);
        }
    }

    rebuildHitMaps();
    ensureScrollBounds();
}

void App::rebuildHitMaps() {
    projectOrder_.resize(projects_.size());
    for (size_t i = 0; i < projects_.size(); ++i) projectOrder_[i] = (int)i;
    std::sort(projectOrder_.begin(), projectOrder_.end(), [&](int a, int b) {
        return projects_[a].updatedAt > projects_[b].updatedAt;
    });

    projectHit_.clear();
    int y = 148 - railScroll_;
    for (size_t i = 0; i < projectOrder_.size(); ++i) {
        projectHit_.push_back({ 16, y, railW_ - 16, y + PROJ_ROW_H - 8 });
        y += PROJ_ROW_H;
    }
    railContentH_ = 148 + (int)projectOrder_.size() * PROJ_ROW_H + 24;

    cardOrder_.clear();
    cardHit_.clear();
    Project* p = activeProject();
    if (!p) { cardContentH_ = 0; return; }

    // Stable vector order (supports drag-reorder)
    cardOrder_.resize(p->entries.size());
    for (size_t i = 0; i < p->entries.size(); ++i) cardOrder_[i] = (int)i;

    const int areaW = (std::max)(1, (int)(rcCards_.right - rcCards_.left));
    const int cols = (std::max)(1, (areaW + CARD_GAP) / (CARD_W + CARD_GAP));
    int col = 0, row = 0;
    for (size_t i = 0; i < cardOrder_.size(); ++i) {
        int x = rcCards_.left + col * (CARD_W + CARD_GAP);
        int cy = rcCards_.top + row * (CARD_H + CARD_GAP) - cardScroll_;
        cardHit_.push_back({ x, cy, x + CARD_W, cy + CARD_H });
        if (++col >= cols) { col = 0; ++row; }
    }
    int rows = cardOrder_.empty() ? 0 : (int)((cardOrder_.size() + cols - 1) / cols);
    cardContentH_ = rows * (CARD_H + CARD_GAP) + 20;
}

void App::ensureScrollBounds() {
    int railView = (std::max)(0, (int)rcRail_.bottom - 148);
    railScroll_ = (std::max)(0, (std::min)(railScroll_, (std::max)(0, railContentH_ - 148 - railView)));
    int cardView = (std::max)(0, (int)(rcCards_.bottom - rcCards_.top));
    cardScroll_ = (std::max)(0, (std::min)(cardScroll_, (std::max)(0, cardContentH_ - cardView)));
}

void App::clearThumbCache() { thumbCache_.clear(); }

Bitmap* App::thumbForEntry(const Entry& e) {
    auto it = thumbCache_.find(e.id);
    if (it != thumbCache_.end()) return it->second.get();

    Bitmap* bmp = nullptr;
    if (!e.filePath.empty() && IsImageExt(e.filePath))
        bmp = MakeImageThumbBmp(e.filePath, CARD_W, 148);
    else if (!e.filePath.empty())
        bmp = MakeFileThumbBmp(e.filePath, CARD_W, 148);
    else {
        std::wstring t = Utf8ToWide(e.body.empty() ? (e.title.empty() ? "Empty note" : e.title) : e.body);
        bmp = MakeTextThumbBmp(t, CARD_W, 148);
    }
    thumbCache_[e.id] = std::unique_ptr<Bitmap>(bmp);
    return bmp;
}

Bitmap* App::thumbForProject(const Project& p) {
    std::string key = "p:" + p.id;
    auto it = thumbCache_.find(key);
    if (it != thumbCache_.end()) return it->second.get();

    Bitmap* bmp = nullptr;
    for (auto& e : p.entries) {
        if (e.type == EntryType::Image && !e.filePath.empty()) {
            bmp = MakeImageThumbBmp(e.filePath, 44, 44);
            break;
        }
    }
    if (!bmp) {
        wchar_t letter = L'?';
        auto name = Utf8ToWide(p.name);
        if (!name.empty()) letter = (wchar_t)towupper(name[0]);
        bmp = MakeLetterThumbBmp(letter, 44, 44);
    }
    thumbCache_[key] = std::unique_ptr<Bitmap>(bmp);
    return bmp;
}

void App::paintRail(Graphics& g) {
    SolidBrush railBr(C_RAIL);
    g.FillRectangle(&railBr, 0, 0, railW_, rcRail_.bottom);

    FillRound(g, RectF(22, 28, 12, 12), 3, C_ACCENT);
    DrawLabel(g, L"Project Board", RectF(42, 22, 220, 28), C_INK, 20, FontStyleBold);
    DrawLabel(g, L"Notes · images · files", RectF(22, 54, 250, 20), C_MUTED, 12);

    DrawButton(g, rcBtnNew_, L"+  New project", true, false,
        hoverHit_ == HIT_NEW, pressHit_ == HIT_NEW);

    int clipH = rcRail_.bottom - 140;
    if (clipH > 0) {
        g.SetClip(Rect(0, 140, railW_, clipH), CombineModeReplace);

        for (size_t i = 0; i < projectOrder_.size(); ++i) {
            const RECT& hr = projectHit_[i];
            if (hr.bottom < 140 || hr.top > rcRail_.bottom) continue;
            int idx = projectOrder_[i];
            auto& p = projects_[idx];
            bool active = idx == activeIndex_;
            bool hover = hoverHit_ == HIT_PROJ_BASE + (int)i;
            RectF r = ToRF(hr);
            if (active) FillRound(g, r, 12, C_ELEV);
            else if (hover) FillRound(g, r, 12, C_SOFT);
            if (active) StrokeRound(g, r, 12, Color(80, 196, 232, 74));

            Bitmap* thumb = thumbForProject(p);
            GraphicsPath tp;
            RoundRectPath(tp, RectF(r.X + 10, r.Y + 8, 40, 40), 8);
            Region old; g.GetClip(&old);
            g.SetClip(&tp, CombineModeIntersect);
            if (thumb) g.DrawImage(thumb, r.X + 10, r.Y + 8, 40.0f, 40.0f);
            g.SetClip(&old);

            std::wstring name = Utf8ToWide(p.name.empty() ? "Untitled" : p.name);
            DrawLabel(g, name.c_str(), RectF(r.X + 60, r.Y + 10, r.Width - 72, 22), C_INK, 14, FontStyleBold);
            std::wstring meta = std::to_wstring(p.entries.size()) + L" entries";
            DrawLabel(g, meta.c_str(), RectF(r.X + 60, r.Y + 32, r.Width - 72, 18), C_MUTED, 11);
        }
        g.ResetClip();
    }

    Pen line(C_LINE, 1);
    g.DrawLine(&line, railW_ - 1, 0, railW_ - 1, rcRail_.bottom);
}

void App::paintStage(Graphics& g) {
    SolidBrush bg(C_BG);
    g.FillRectangle(&bg, railW_, 0, rcStage_.right - railW_, rcStage_.bottom);

    LinearGradientBrush glow(
        Point(railW_, 0), Point(rcStage_.right, 260),
        Color(24, 196, 232, 74), Color(0, 196, 232, 74));
    g.FillRectangle(&glow, railW_, 0, rcStage_.right - railW_, 260);

    Project* p = activeProject();
    if (!p) {
        DrawLabel(g, L"No project open", RectF((REAL)railW_ + 36, 200, 400, 40), C_INK, 32, FontStyleBold);
        DrawLabel(g, L"Create a project from the rail to begin.",
            RectF((REAL)railW_ + 36, 248, 420, 28), C_MUTED, 15);
        return;
    }

    FillRound(g, ToRF(rcName_), 10, C_ELEV);
    StrokeRound(g, ToRF(rcName_), 10, C_LINE);
    DrawButton(g, rcBtnEditProj_, L"Edit", false, false, hoverHit_ == HIT_EDIT_PROJ, pressHit_ == HIT_EDIT_PROJ);
    DrawButton(g, rcBtnDelProj_, L"Delete", false, true, hoverHit_ == HIT_DEL_PROJ, pressHit_ == HIT_DEL_PROJ);
    DrawButton(g, rcBtnAddText_, L"Add text", false, false, hoverHit_ == HIT_ADD_TEXT, pressHit_ == HIT_ADD_TEXT);
    DrawButton(g, rcBtnAddImg_, L"Add image", false, false, hoverHit_ == HIT_ADD_IMG, pressHit_ == HIT_ADD_IMG);
    DrawButton(g, rcBtnAddFile_, L"Add file", false, false, hoverHit_ == HIT_ADD_FILE, pressHit_ == HIT_ADD_FILE);
    DrawButton(g, rcBtnDelEntry_, L"Delete entry", false, false, hoverHit_ == HIT_DEL_ENTRY, pressHit_ == HIT_DEL_ENTRY);

    int textN = 0, imgN = 0, fileN = 0;
    for (auto& e : p->entries) {
        if (e.type == EntryType::Image) ++imgN;
        else if (e.type == EntryType::File) ++fileN;
        else ++textN;
    }
    std::wstring status = std::to_wstring(p->entries.size()) + L" items   ·   "
        + std::to_wstring(textN) + L" text   ·   " + std::to_wstring(imgN)
        + L" images   ·   " + std::to_wstring(fileN) + L" files   ·   " + FormatTime(p->updatedAt);
    DrawLabel(g, status.c_str(),
        RectF((REAL)rcCards_.left, 122, (REAL)(rcCards_.right - rcCards_.left), 16), C_MUTED, 12);

    if (!p->description.empty()) {
        // shown in edit dialog; keep stage clean
    }

    int cw = rcCards_.right - rcCards_.left;
    int ch = rcCards_.bottom - rcCards_.top;
    if (cw > 0 && ch > 0)
        g.SetClip(Rect(rcCards_.left, rcCards_.top, cw, ch), CombineModeReplace);

    if (cardOrder_.empty()) {
        DrawLabel(g, L"Empty project", RectF((REAL)rcCards_.left, (REAL)rcCards_.top + 40, 300, 30), C_INK, 22, FontStyleBold);
        DrawLabel(g, L"Add text, images, or any files.",
            RectF((REAL)rcCards_.left, (REAL)rcCards_.top + 74, 360, 24), C_MUTED, 14);
    }

    for (size_t i = 0; i < cardOrder_.size(); ++i) {
        const RECT& hr = cardHit_[i];
        if (hr.bottom < rcCards_.top || hr.top > rcCards_.bottom) continue;
        int ei = cardOrder_[i];
        auto& e = p->entries[ei];
        bool hover = hoverHit_ == HIT_CARD_BASE + (int)i;
        bool selected = selectedEntryId_ == e.id;
        RectF r = ToRF(hr);
        FillRound(g, r, 14, hover || selected ? C_CARD_HOVER : C_ELEV);
        StrokeRound(g, r, 14, selected ? Color(200, 196, 232, 74) : (hover ? Color(120, 196, 232, 74) : C_LINE),
            (hover || selected) ? 1.5f : 1.0f);

        GraphicsPath clip;
        RoundRectPath(clip, RectF(r.X + 1, r.Y + 1, r.Width - 2, 148), 13);
        Region old; g.GetClip(&old);
        g.SetClip(&clip, CombineModeIntersect);
        Bitmap* thumb = thumbForEntry(e);
        if (thumb) g.DrawImage(thumb, r.X + 1, r.Y + 1, r.Width - 2, 148.0f);
        g.SetClip(&old);

        std::wstring title = Utf8ToWide(e.title.empty()
            ? (e.type == EntryType::Image ? "Image" : (e.type == EntryType::File ? "File" : "Untitled note"))
            : e.title);
        DrawLabel(g, title.c_str(), RectF(r.X + 12, r.Y + 160, r.Width - 24, 24), C_INK, 14, FontStyleBold);
        DrawLabel(g, FormatTime(e.updatedAt).c_str(), RectF(r.X + 12, r.Y + 186, r.Width - 24, 18), C_MUTED, 11);
    }

    // Drag ghost
    if (dragging_ && dragFromIdx_ >= 0 && dragFromIdx_ < (int)cardOrder_.size()) {
        int ei = cardOrder_[dragFromIdx_];
        auto& e = p->entries[ei];
        RectF r((REAL)(dragPos_.x - CARD_W / 2), (REAL)(dragPos_.y - 40), (REAL)CARD_W, (REAL)CARD_H);
        FillRound(g, r, 14, Color(220, 38, 48, 42));
        StrokeRound(g, r, 14, Color(220, 196, 232, 74), 2.0f);
        Bitmap* thumb = thumbForEntry(e);
        if (thumb) g.DrawImage(thumb, r.X + 1, r.Y + 1, r.Width - 2, 148.0f);
        std::wstring title = Utf8ToWide(e.title.empty() ? "…" : e.title);
        DrawLabel(g, title.c_str(), RectF(r.X + 12, r.Y + 160, r.Width - 24, 24), C_INK, 14, FontStyleBold);
    }

    // Drop target highlight
    if (dragging_ && dragOverIdx_ >= 0 && dragOverIdx_ < (int)cardHit_.size()) {
        StrokeRound(g, ToRF(cardHit_[dragOverIdx_]), 14, C_ACCENT, 2.5f);
    }

    g.ResetClip();
}

void App::paint(HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    Bitmap back(rc.right, rc.bottom, PixelFormat32bppARGB);
    Graphics g(&back);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    paintRail(g);
    paintStage(g);
    BlitBitmap(hdc, back, rc.right, rc.bottom);
}

int App::hitTest(int x, int y) const {
    if (PtIn(rcBtnNew_, x, y)) return HIT_NEW;
    if (PtIn(rcBtnEditProj_, x, y)) return HIT_EDIT_PROJ;
    if (PtIn(rcBtnDelProj_, x, y)) return HIT_DEL_PROJ;
    if (PtIn(rcBtnAddText_, x, y)) return HIT_ADD_TEXT;
    if (PtIn(rcBtnAddImg_, x, y)) return HIT_ADD_IMG;
    if (PtIn(rcBtnAddFile_, x, y)) return HIT_ADD_FILE;
    if (PtIn(rcBtnDelEntry_, x, y)) return HIT_DEL_ENTRY;
    for (size_t i = 0; i < projectHit_.size(); ++i) {
        if (projectHit_[i].top >= 140 && PtIn(projectHit_[i], x, y))
            return HIT_PROJ_BASE + (int)i;
    }
    for (size_t i = 0; i < cardHit_.size(); ++i) {
        if (PtIn(cardHit_[i], x, y) && cardHit_[i].bottom > rcCards_.top && cardHit_[i].top < rcCards_.bottom)
            return HIT_CARD_BASE + (int)i;
    }
    return HIT_NONE;
}

int App::cardIndexAt(int x, int y) const {
    for (size_t i = 0; i < cardHit_.size(); ++i) {
        if (PtIn(cardHit_[i], x, y) && cardHit_[i].bottom > rcCards_.top && cardHit_[i].top < rcCards_.bottom)
            return (int)i;
    }
    return -1;
}

void App::reorderEntry(int fromIndex, int toIndex) {
    Project* p = activeProject();
    if (!p) return;
    if (fromIndex < 0 || toIndex < 0) return;
    if (fromIndex >= (int)p->entries.size() || toIndex >= (int)p->entries.size()) return;
    if (fromIndex == toIndex) return;

    Entry moved = std::move(p->entries[fromIndex]);
    p->entries.erase(p->entries.begin() + fromIndex);
    p->entries.insert(p->entries.begin() + toIndex, std::move(moved));
    p->updatedAt = NowMs();
    persist();
    clearThumbCache();
    layout();
    invalidateUi();
}

void App::setHover(int hit) {
    if (hit == hoverHit_) return;
    hoverHit_ = hit;
    invalidateUi();
}

void App::invalidateUi() { InvalidateRect(hwnd_, nullptr, FALSE); }

Project* App::activeProject() {
    if (activeIndex_ < 0 || activeIndex_ >= (int)projects_.size()) return nullptr;
    return &projects_[activeIndex_];
}

Entry* App::findEntryById(const std::string& id) {
    Project* p = activeProject();
    if (!p) return nullptr;
    for (auto& e : p->entries) if (e.id == id) return &e;
    return nullptr;
}

int App::findEntryIndexById(const std::string& id) {
    Project* p = activeProject();
    if (!p) return -1;
    for (int i = 0; i < (int)p->entries.size(); ++i)
        if (p->entries[i].id == id) return i;
    return -1;
}

void App::selectProject(int index) {
    if (index < 0 || index >= (int)projects_.size()) return;
    activeIndex_ = index;
    cardScroll_ = 0;
    selectedEntryId_.clear();
    clearThumbCache();
    Project* p = activeProject();
    if (p && hwndName_) {
        SetWindowTextW(hwndName_, Utf8ToWide(p->name).c_str());
        ShowWindow(hwndName_, SW_SHOW);
    }
    layout();
    invalidateUi();
}

void App::persist() {
    try { storage_.save(projects_); }
    catch (...) {}
}

void App::onNewProject() {
    ModalState st;
    st.isNewProject = true;
    if (!RunModernModal(hwnd_, hInst_, st, L"New project", 460, 320)) return;
    Project p;
    p.id = MakeId();
    p.name = WideToUtf8(st.title.empty() ? L"Untitled project" : st.title);
    p.description = WideToUtf8(st.description);
    p.createdAt = p.updatedAt = NowMs();
    projects_.push_back(p);
    std::error_code ec;
    fs::create_directories(storage_.projectDir(p.id), ec);
    persist();
    clearThumbCache();
    selectProject((int)projects_.size() - 1);
}

void App::onEditProject() {
    Project* p = activeProject();
    if (!p) return;
    ModalState st;
    st.isEditProject = true;
    st.title = Utf8ToWide(p->name);
    st.description = Utf8ToWide(p->description);
    if (!RunModernModal(hwnd_, hInst_, st, L"Edit project", 460, 320)) return;
    p->name = WideToUtf8(st.title.empty() ? L"Untitled project" : st.title);
    p->description = WideToUtf8(st.description);
    p->updatedAt = NowMs();
    if (hwndName_) SetWindowTextW(hwndName_, Utf8ToWide(p->name).c_str());
    persist();
    clearThumbCache();
    layout();
    invalidateUi();
}

void App::onDeleteProject() {
    Project* p = activeProject();
    if (!p) return;
    std::wstring msg = L"Delete project \"" + Utf8ToWide(p->name) + L"\" and all entries?";
    if (MessageBoxW(hwnd_, msg.c_str(), L"Delete project", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    for (auto& e : p->entries)
        if (!e.filePath.empty()) DeleteFileW(e.filePath.c_str());
    projects_.erase(projects_.begin() + activeIndex_);
    activeIndex_ = projects_.empty() ? -1 : (std::min)(activeIndex_, (int)projects_.size() - 1);
    selectedEntryId_.clear();
    persist();
    clearThumbCache();
    layout();
    if (activeIndex_ >= 0) selectProject(activeIndex_);
    else {
        ShowWindow(hwndName_, SW_HIDE);
        invalidateUi();
    }
}

void App::onAddText() {
    Project* p = activeProject();
    if (!p) return;
    Entry e;
    e.id = MakeId();
    e.title = "New note";
    e.type = EntryType::Text;
    e.createdAt = e.updatedAt = NowMs();
    p->entries.push_back(std::move(e));
    p->updatedAt = NowMs();
    std::string id = p->entries.back().id;
    persist();
    clearThumbCache();
    selectedEntryId_ = id;
    layout();
    invalidateUi();
}

void App::importFiles(bool imagesOnly) {
    Project* p = activeProject();
    if (!p) return;

    wchar_t fileBuf[8192] = L"";
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = imagesOnly
        ? L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff\0All\0*.*\0"
        : L"All files\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = 8192;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = imagesOnly ? L"Add image(s)" : L"Add file(s)";
    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<std::wstring> files;
    wchar_t* ptr = ofn.lpstrFile;
    std::wstring dir = ptr;
    ptr += dir.size() + 1;
    if (*ptr == 0) files.push_back(dir);
    else while (*ptr) { files.push_back(dir + L"\\" + ptr); ptr += wcslen(ptr) + 1; }

    std::string lastId;
    for (auto& src : files) {
        if (imagesOnly && !IsImageExt(src)) continue;
        Entry e;
        e.id = MakeId();
        e.createdAt = e.updatedAt = NowMs();
        e.title = WideToUtf8(fs::path(src).stem().wstring());
        e.type = IsImageExt(src) ? EntryType::Image : EntryType::File;
        std::wstring dest;
        if (!storage_.copyFileToEntry(src, p->id, e.id, dest)) continue;
        e.filePath = dest;
        lastId = e.id;
        p->entries.push_back(std::move(e));
    }
    if (!lastId.empty()) {
        p->updatedAt = NowMs();
        persist();
        clearThumbCache();
        selectedEntryId_ = lastId;
        layout();
        invalidateUi();
    }
}

void App::onAddImage() { importFiles(true); }
void App::onAddFile() { importFiles(false); }

void App::onDeleteEntry() {
    Project* p = activeProject();
    if (!p) return;
    std::string id = selectedEntryId_;
    if (id.empty() && hoverHit_ >= HIT_CARD_BASE) {
        int oi = hoverHit_ - HIT_CARD_BASE;
        if (oi >= 0 && oi < (int)cardOrder_.size())
            id = p->entries[cardOrder_[oi]].id;
    }
    int ei = findEntryIndexById(id);
    if (ei < 0) {
        MessageBoxW(hwnd_, L"Select a card first, then Delete entry.", L"Delete entry", MB_OK);
        return;
    }
    if (MessageBoxW(hwnd_, L"Delete this entry?", L"Delete entry", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (!p->entries[ei].filePath.empty()) DeleteFileW(p->entries[ei].filePath.c_str());
    p->entries.erase(p->entries.begin() + ei);
    selectedEntryId_.clear();
    p->updatedAt = NowMs();
    persist();
    clearThumbCache();
    layout();
    invalidateUi();
}

void App::onOpenEntry(const std::string& entryId) {
    Project* p = activeProject();
    Entry* e = findEntryById(entryId);
    if (!p || !e) return;
    selectedEntryId_ = entryId;

    if (!e->filePath.empty()) {
        if (IsImageExt(e->filePath)) e->type = EntryType::Image;
        else if (e->type == EntryType::Text) e->type = EntryType::File;
    }

    ModalState st;
    st.type = (e->type == EntryType::Image) ? EntryType::Image
        : (e->type == EntryType::Text ? EntryType::Text : EntryType::File);
    st.title = Utf8ToWide(e->title);
    st.body = Utf8ToWide(e->body);
    st.filePath = e->filePath;
    st.caption = e->type == EntryType::Text ? L"Edit note"
        : (IsVideoExt(e->filePath) ? L"Edit video"
            : (IsImageExt(e->filePath) ? L"Edit image" : L"Edit file"));

    int mw = (e->type == EntryType::Text) ? 560 : 740;
    int mh = e->type == EntryType::Text ? 520 : 560;
    if (!RunModernModal(hwnd_, hInst_, st, st.caption.c_str(), mw, mh)) {
        invalidateUi();
        return;
    }

    e = findEntryById(entryId);
    if (!e) return;
    int ei = findEntryIndexById(entryId);

    if (st.deleted) {
        if (!e->filePath.empty()) DeleteFileW(e->filePath.c_str());
        p->entries.erase(p->entries.begin() + ei);
        selectedEntryId_.clear();
        p->updatedAt = NowMs();
        persist();
        clearThumbCache();
        layout();
        invalidateUi();
        return;
    }

    e->title = WideToUtf8(st.title);
    e->body = WideToUtf8(st.body);
    e->updatedAt = NowMs();

    if (!st.filePath.empty() && st.filePath != e->filePath) {
        std::wstring dest;
        if (storage_.copyFileToEntry(st.filePath, p->id, e->id, dest)) {
            if (!e->filePath.empty() && e->filePath != dest) DeleteFileW(e->filePath.c_str());
            e->filePath = dest;
            e->type = IsImageExt(dest) ? EntryType::Image : EntryType::File;
        }
    } else if (!e->filePath.empty()) {
        e->type = IsImageExt(e->filePath) ? EntryType::Image : EntryType::File;
    }

    p->updatedAt = NowMs();
    persist();
    clearThumbCache();
    layout();
    invalidateUi();
}

void App::onViewMedia(const std::string& entryId) {
    Entry* e = findEntryById(entryId);
    if (!e || e->filePath.empty()) return;
    selectedEntryId_ = entryId;
    ShowMediaViewer(hwnd_, hInst_, e->filePath, Utf8ToWide(e->title));
    invalidateUi();
}

void App::onProjectNameChanged() {
    Project* p = activeProject();
    if (!p || !hwndName_) return;
    wchar_t buf[256]{};
    GetWindowTextW(hwndName_, buf, 256);
    p->name = WideToUtf8(buf);
    p->updatedAt = NowMs();
    persist();
    // Don't clear all thumbs — just project letter thumbs
    for (auto it = thumbCache_.begin(); it != thumbCache_.end(); ) {
        if (it->first.rfind("p:", 0) == 0) it = thumbCache_.erase(it);
        else ++it;
    }
    rebuildHitMaps();
    invalidateUi();
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->handleMessage(hwnd, msg, wParam, lParam);
}

LRESULT App::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        createChildren();
        if (!projects_.empty()) selectProject(0);
        return 0;
    case WM_SIZE:
        layout();
        invalidateUi();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(236, 240, 234));
        SetBkColor(hdc, RGB(32, 39, 35));
        static HBRUSH br = CreateSolidBrush(RGB(32, 39, 35));
        return (LRESULT)br;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            if (hitTest(pt.x, pt.y) != HIT_NONE) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        dragPos_ = { x, y };

        if (dragging_ || (pressHit_ >= HIT_CARD_BASE && (GetCapture() == hwnd))) {
            int dx = x - dragStart_.x;
            int dy = y - dragStart_.y;
            if (!dragging_ && (dx * dx + dy * dy > 36)) {
                dragging_ = true;
                dragMoved_ = true;
                dragFromIdx_ = pressHit_ - HIT_CARD_BASE;
            }
            if (dragging_) {
                dragOverIdx_ = cardIndexAt(x, y);
                invalidateUi();
                return 0;
            }
        }

        setHover(hitTest(x, y));
        return 0;
    }
    case WM_MOUSELEAVE:
        if (!dragging_) {
            setHover(HIT_NONE);
            pressHit_ = HIT_NONE;
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        pressHit_ = hitTest(x, y);
        dragStart_ = { x, y };
        dragPos_ = dragStart_;
        dragging_ = false;
        dragMoved_ = false;
        dragFromIdx_ = -1;
        dragOverIdx_ = -1;
        SetCapture(hwnd);
        invalidateUi();
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int hit = hitTest(x, y);
        int pressed = pressHit_;
        bool wasDragging = dragging_;
        int fromIdx = dragFromIdx_;
        int overIdx = dragOverIdx_;

        pressHit_ = HIT_NONE;
        dragging_ = false;
        dragFromIdx_ = -1;
        dragOverIdx_ = -1;
        ReleaseCapture();

        if (wasDragging && fromIdx >= 0) {
            int toIdx = overIdx >= 0 ? overIdx : cardIndexAt(x, y);
            if (toIdx >= 0 && toIdx != fromIdx
                && fromIdx < (int)cardOrder_.size() && toIdx < (int)cardOrder_.size()) {
                reorderEntry(cardOrder_[fromIdx], cardOrder_[toIdx]);
            } else {
                invalidateUi();
            }
            return 0;
        }

        if (hit == pressed && hit != HIT_NONE && !dragMoved_) {
            if (hit == HIT_NEW) onNewProject();
            else if (hit == HIT_EDIT_PROJ) onEditProject();
            else if (hit == HIT_DEL_PROJ) onDeleteProject();
            else if (hit == HIT_ADD_TEXT) onAddText();
            else if (hit == HIT_ADD_IMG) onAddImage();
            else if (hit == HIT_ADD_FILE) onAddFile();
            else if (hit == HIT_DEL_ENTRY) onDeleteEntry();
            else if (hit >= HIT_PROJ_BASE && hit < HIT_CARD_BASE) {
                int oi = hit - HIT_PROJ_BASE;
                if (oi >= 0 && oi < (int)projectOrder_.size())
                    selectProject(projectOrder_[oi]);
            } else if (hit >= HIT_CARD_BASE) {
                int oi = hit - HIT_CARD_BASE;
                Project* p = activeProject();
                if (p && oi >= 0 && oi < (int)cardOrder_.size()) {
                    selectedEntryId_ = p->entries[cardOrder_[oi]].id;
                    invalidateUi();
                }
            }
        }
        invalidateUi();
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int hit = hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit >= HIT_CARD_BASE) {
            int oi = hit - HIT_CARD_BASE;
            Project* p = activeProject();
            if (p && oi >= 0 && oi < (int)cardOrder_.size()) {
                selectedEntryId_ = p->entries[cardOrder_[oi]].id;
                onOpenEntry(selectedEntryId_);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (pt.x < railW_) railScroll_ -= delta / 2;
        else cardScroll_ -= delta / 2;
        ensureScrollBounds();
        rebuildHitMaps();
        invalidateUi();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_EDIT_NAME && HIWORD(wParam) == EN_KILLFOCUS)
            onProjectNameChanged();
        return 0;
    case WM_DESTROY:
        onProjectNameChanged();
        persist();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
