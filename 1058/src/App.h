#pragma once

#include "Models.h"
#include "Storage.h"

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

class App {
public:
    explicit App(HINSTANCE hInst);
    int run(int nCmdShow);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);

    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);
    bool createWindow(int nCmdShow);
    void createChildren();
    void layout();
    void paint(HDC hdc);
    void paintRail(Gdiplus::Graphics& g);
    void paintStage(Gdiplus::Graphics& g);
    void rebuildHitMaps();
    void selectProject(int index);
    Project* activeProject();
    Entry* findEntryById(const std::string& id);
    int findEntryIndexById(const std::string& id);

    void onNewProject();
    void onEditProject();
    void onDeleteProject();
    void onAddText();
    void onAddImage();
    void onAddFile();
    void onDeleteEntry();
    void onOpenEntry(const std::string& entryId);
    void onViewMedia(const std::string& entryId);
    void onProjectNameChanged();
    void persist();
    void invalidateUi();
    void importFiles(bool imagesOnly);
    void reorderEntry(int fromIndex, int toIndex);

    Gdiplus::Bitmap* thumbForEntry(const Entry& e);
    Gdiplus::Bitmap* thumbForProject(const Project& p);
    void clearThumbCache();

    int hitTest(int x, int y) const;
    int cardIndexAt(int x, int y) const;
    void setHover(int hit);
    void ensureScrollBounds();

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND hwndName_ = nullptr;
    WNDPROC oldEditProc_ = nullptr;

    Storage storage_;
    std::vector<Project> projects_;
    int activeIndex_ = -1;

    int railW_ = 300;
    RECT rcRail_{}, rcStage_{}, rcCards_{}, rcName_{};
    RECT rcBtnNew_{}, rcBtnEditProj_{}, rcBtnDelProj_{};
    RECT rcBtnAddText_{}, rcBtnAddImg_{}, rcBtnAddFile_{}, rcBtnDelEntry_{};
    std::vector<RECT> projectHit_;
    std::vector<int> projectOrder_;
    std::vector<RECT> cardHit_;
    std::vector<int> cardOrder_;

    int hoverHit_ = -1;
    int pressHit_ = -1;
    std::string selectedEntryId_;
    int railScroll_ = 0;
    int cardScroll_ = 0;
    int railContentH_ = 0;
    int cardContentH_ = 0;

    // drag reorder
    bool dragging_ = false;
    bool dragMoved_ = false;
    int dragFromIdx_ = -1;
    int dragOverIdx_ = -1;
    POINT dragStart_{};
    POINT dragPos_{};

    std::unordered_map<std::string, std::unique_ptr<Gdiplus::Bitmap>> thumbCache_;
};
