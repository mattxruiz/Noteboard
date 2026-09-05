#pragma once

#include <windows.h>
#include <string>

// Shows image fullscreen-ish, or plays video with MFPlay. Blocks until closed.
// Also offers Show-in-folder via button inside the viewer.
bool ShowMediaViewer(HWND owner, HINSTANCE inst, const std::wstring& filePath, const std::wstring& title);

// Opens Explorer with the file selected.
void ShowInFolder(HWND owner, const std::wstring& filePath);

bool IsVideoExt(const std::wstring& path);
bool IsImageFile(const std::wstring& path);
