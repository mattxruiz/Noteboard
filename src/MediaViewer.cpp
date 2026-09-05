#include "MediaViewer.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mfplay.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {

const Color C_BG(255, 12, 14, 13);
const Color C_BAR(255, 28, 34, 30);
const Color C_INK(255, 236, 240, 234);
const Color C_MUTED(255, 148, 162, 150);
const Color C_ACCENT(255, 196, 232, 74);
const Color C_SOFT(255, 42, 52, 46);

std::wstring LowerExt(const std::wstring& path) {
    auto e = fs::path(path).extension().wstring();
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

void RoundRectPath(GraphicsPath& path, RectF r, REAL radius) {
    if (r.Width < 1.f || r.Height < 1.f) { path.AddRectangle(r); return; }
    radius = (std::min)(radius, (std::min)(r.Width, r.Height) * 0.5f);
    REAL d = radius * 2.f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRound(Graphics& g, const RectF& r, REAL radius, const Color& c) {
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    SolidBrush br(c);
    g.FillPath(&br, &path);
}

void DrawLabel(Graphics& g, const wchar_t* text, const RectF& r, const Color& c, REAL size, bool bold = false) {
    FontFamily ff(L"Segoe UI");
    Font font(&ff, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush br(c);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text, -1, &font, r, &fmt, &br);
}

bool PtIn(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct Btn { RECT rc{}; const wchar_t* label = L""; int id = 0; bool primary = false; };

struct ViewerState {
    std::wstring path;
    std::wstring title;
    bool isVideo = false;
    bool closed = false;
    bool playing = false;
    int hover = 0;
    int press = 0;
    Bitmap* image = nullptr;
    IMFPMediaPlayer* player = nullptr;
    HWND hwndVideo = nullptr;
    std::vector<Btn> buttons;
    RECT rcMedia{};
    int width = 0;
    int height = 0;
};

void LayoutButtons(ViewerState& st) {
    st.buttons.clear();
    int by = st.height - 56;
    int x = 24;
    st.buttons.push_back({ { x, by, x + 140, by + 36 }, L"Show in folder", 1, false });
    x += 152;
    if (st.isVideo) {
        st.buttons.push_back({ { x, by, x + 100, by + 36 }, st.playing ? L"Pause" : L"Play", 2, true });
        x += 112;
    }
    st.buttons.push_back({ { st.width - 120, by, st.width - 24, by + 36 }, L"Close", 3, false });
}

void DrawButton(Graphics& g, const Btn& b, bool hover, bool press) {
    RectF r((REAL)b.rc.left, (REAL)b.rc.top, (REAL)(b.rc.right - b.rc.left), (REAL)(b.rc.bottom - b.rc.top));
    Color fill = b.primary ? C_ACCENT : C_SOFT;
    if (hover) fill = b.primary ? Color(255, 212, 242, 100) : Color(255, 52, 64, 56);
    if (press) fill = Color(255, 140, 170, 40);
    FillRound(g, r, 10, fill);
    Color ink = b.primary ? Color(255, 18, 22, 20) : C_INK;
    DrawLabel(g, b.label, r, ink, 13, true);
}

void StopPlayer(ViewerState& st) {
    if (st.player) {
        st.player->Shutdown();
        st.player->Release();
        st.player = nullptr;
    }
    st.playing = false;
}

bool StartPlayer(ViewerState& st, HWND /*hwnd*/) {
    StopPlayer(st);
    if (!st.hwndVideo) return false;
    HRESULT hr = MFPCreateMediaPlayer(
        st.path.c_str(),
        FALSE,
        0,
        nullptr,
        st.hwndVideo,
        &st.player);
    if (FAILED(hr) || !st.player) return false;
    st.player->Play();
    st.playing = true;
    return true;
}

} // namespace

bool IsVideoExt(const std::wstring& path) {
    auto e = LowerExt(path);
    return e == L".mp4" || e == L".m4v" || e == L".mov" || e == L".avi"
        || e == L".wmv" || e == L".mkv" || e == L".webm" || e == L".mpeg" || e == L".mpg";
}

bool IsImageFile(const std::wstring& path) {
    auto e = LowerExt(path);
    return e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".bmp"
        || e == L".gif" || e == L".webp" || e == L".tif" || e == L".tiff";
}

void ShowInFolder(HWND owner, const std::wstring& filePath) {
    if (filePath.empty()) return;
    std::wstring path = filePath;
    for (auto& c : path) if (c == L'/') c = L'\\';

    // Prefer select-in-explorer
    std::wstring params = L"/select,\"" + path + L"\"";
    HINSTANCE hi = ShellExecuteW(owner, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)hi > 32) return;

    // Fallback: open parent folder
    auto parent = fs::path(path).parent_path().wstring();
    if (!parent.empty())
        ShellExecuteW(owner, L"open", parent.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool ShowMediaViewer(HWND owner, HINSTANCE inst, const std::wstring& filePath, const std::wstring& title) {
    if (filePath.empty() || !fs::exists(filePath)) {
        MessageBoxW(owner, L"File not found on disk.", L"Media", MB_OK | MB_ICONWARNING);
        return false;
    }

    ViewerState st;
    st.path = filePath;
    st.title = title.empty() ? fs::path(filePath).filename().wstring() : title;
    st.isVideo = IsVideoExt(filePath);
    st.width = 960;
    st.height = 680;

    if (!st.isVideo) {
        st.image = Bitmap::FromFile(filePath.c_str());
        if (!st.image || st.image->GetLastStatus() != Ok || st.image->GetWidth() == 0) {
            delete st.image;
            st.image = nullptr;
            MessageBoxW(owner, L"Could not load image.", L"Media", MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* st = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg) {
            case WM_CREATE: {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                st = reinterpret_cast<ViewerState*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
                st->rcMedia = { 16, 48, st->width - 16, st->height - 72 };
                if (st->isVideo) {
                    st->hwndVideo = CreateWindowExW(0, L"STATIC", L"",
                        WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
                        st->rcMedia.left, st->rcMedia.top,
                        st->rcMedia.right - st->rcMedia.left,
                        st->rcMedia.bottom - st->rcMedia.top,
                        hwnd, nullptr, cs->hInstance, nullptr);
                    StartPlayer(*st, hwnd);
                }
                LayoutButtons(*st);
                return 0;
            }
            case WM_SIZE: {
                if (!st) return 0;
                RECT rc; GetClientRect(hwnd, &rc);
                st->width = rc.right;
                st->height = rc.bottom;
                st->rcMedia = { 16, 48, st->width - 16, st->height - 72 };
                if (st->hwndVideo) {
                    SetWindowPos(st->hwndVideo, nullptr,
                        st->rcMedia.left, st->rcMedia.top,
                        st->rcMedia.right - st->rcMedia.left,
                        st->rcMedia.bottom - st->rcMedia.top,
                        SWP_NOZORDER);
                    if (st->player) st->player->UpdateVideo();
                }
                LayoutButtons(*st);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                if (!st) break;
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc; GetClientRect(hwnd, &rc);
                Bitmap back(rc.right, rc.bottom, PixelFormat32bppARGB);
                Graphics g(&back);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
                g.Clear(C_BG);

                SolidBrush bar(C_BAR);
                g.FillRectangle(&bar, 0, 0, rc.right, 44);
                SolidBrush accent(C_ACCENT);
                g.FillRectangle(&accent, 0, 0, rc.right, 3);

                FontFamily ff(L"Segoe UI");
                Font font(&ff, 16, FontStyleBold, UnitPixel);
                SolidBrush ink(C_INK);
                StringFormat fmt;
                fmt.SetTrimming(StringTrimmingEllipsisCharacter);
                g.DrawString(st->title.c_str(), -1, &font, PointF(20, 12), &ink);

                if (!st->isVideo && st->image) {
                    REAL mw = (REAL)(st->rcMedia.right - st->rcMedia.left);
                    REAL mh = (REAL)(st->rcMedia.bottom - st->rcMedia.top);
                    REAL iw = (REAL)st->image->GetWidth();
                    REAL ih = (REAL)st->image->GetHeight();
                    if (iw > 0 && ih > 0 && mw > 0 && mh > 0) {
                        REAL scale = (std::min)(mw / iw, mh / ih);
                        REAL dw = iw * scale, dh = ih * scale;
                        REAL dx = st->rcMedia.left + (mw - dw) * 0.5f;
                        REAL dy = st->rcMedia.top + (mh - dh) * 0.5f;
                        g.DrawImage(st->image, dx, dy, dw, dh);
                    }
                }

                g.FillRectangle(&bar, 0, rc.bottom - 64, rc.right, 64);
                for (auto& b : st->buttons)
                    DrawButton(g, b, st->hover == b.id, st->press == b.id);

                HBITMAP hb = nullptr;
                back.GetHBITMAP(Color(0, 0, 0, 0), &hb);
                if (hb) {
                    HDC mem = CreateCompatibleDC(hdc);
                    auto old = SelectObject(mem, hb);
                    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
                    SelectObject(mem, old);
                    DeleteObject(hb);
                    DeleteDC(mem);
                }
                EndPaint(hwnd, &ps);
                return 0;
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
                for (auto& b : st->buttons) {
                    if (b.id != pressed || !PtIn(b.rc, x, y)) continue;
                    if (b.id == 1) ShowInFolder(hwnd, st->path);
                    else if (b.id == 2 && st->player) {
                        MFP_MEDIAPLAYER_STATE state = MFP_MEDIAPLAYER_STATE_EMPTY;
                        st->player->GetState(&state);
                        if (state == MFP_MEDIAPLAYER_STATE_PLAYING) {
                            st->player->Pause();
                            st->playing = false;
                        } else {
                            st->player->Play();
                            st->playing = true;
                        }
                        LayoutButtons(*st);
                    } else if (b.id == 3) {
                        DestroyWindow(hwnd);
                    }
                    break;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE) DestroyWindow(hwnd);
                else if (wp == VK_SPACE && st && st->player) {
                    MFP_MEDIAPLAYER_STATE state = MFP_MEDIAPLAYER_STATE_EMPTY;
                    st->player->GetState(&state);
                    if (state == MFP_MEDIAPLAYER_STATE_PLAYING) {
                        st->player->Pause();
                        st->playing = false;
                    } else {
                        st->player->Play();
                        st->playing = true;
                    }
                    LayoutButtons(*st);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                if (st) {
                    StopPlayer(*st);
                    delete st->image;
                    st->image = nullptr;
                    st->closed = true;
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        };
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"ProjectBoardMediaViewer";
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT orc{};
    GetWindowRect(owner, &orc);
    int x = orc.left + ((orc.right - orc.left) - st.width) / 2;
    int y = orc.top + ((orc.bottom - orc.top) - st.height) / 2;

    HWND dlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"ProjectBoardMediaViewer", st.title.c_str(),
        WS_POPUP | WS_VISIBLE | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX,
        x, y, st.width, st.height, owner, nullptr, inst, &st);
    if (!dlg) {
        delete st.image;
        return false;
    }

    EnableWindow(owner, FALSE);
    MSG msg{};
    while (!st.closed && IsWindow(dlg)) {
        BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
        if (ret == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (ret == -1) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return true;
}
