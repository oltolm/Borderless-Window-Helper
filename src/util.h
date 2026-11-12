#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

#pragma warning(disable : 4800 4267 4996)

std::filesystem::path AppPath();
HRESULT createShortcut(const std::filesystem::path &linkFileName, const std::filesystem::path &targetPath,
                       const std::wstring &arguments, const std::wstring &description);
std::filesystem::path GetSysFolderLocation(int csidl);
std::wstring GetClassNameString(HWND hWnd);
std::wstring GetWindowTextString(HWND hWnd);
