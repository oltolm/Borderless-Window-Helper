#include "util.h"
#include <psapi.h>
#include <shlobj.h>
#include <wrl.h>

using namespace std;
using namespace Microsoft;

std::filesystem::path AppPath()
{
    std::wstring filename;
    filename.resize(MAX_PATH);
    boolean truncated;
    do
    {
        DWORD nRet = ::GetModuleFileNameW(NULL, &filename[0], filename.size());
        if (nRet == 0)
            return L"";
        truncated = nRet == filename.size();
        filename.resize(truncated ? filename.size() * 2 : nRet);
    } while (truncated);
    return filename;
}

std::filesystem::path GetModuleFileNameExPath(HANDLE hProcess)
{
    std::wstring filename;
    filename.resize(MAX_PATH);
    boolean truncated;
    do
    {
        DWORD nRet = ::GetModuleFileNameExW(hProcess, NULL, &filename[0], filename.size());
        if (nRet == 0)
            return L"";
        truncated = nRet == filename.size();
        filename.resize(truncated ? filename.size() * 2 : nRet);
    } while (truncated);
    return filename;
}

HRESULT createShortcut(const std::filesystem::path &linkFileName, const std::filesystem::path &targetPath,
                       const std::wstring &arguments, const std::wstring &description)
{
    HRESULT hres;
    WRL::ComPtr<IShellLinkW> psl;
    hres = ::CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, &psl);
    if (SUCCEEDED(hres))
    {
        WRL::ComPtr<IPersistFile> ppf;
        psl->SetPath(targetPath.c_str());
        psl->SetArguments(arguments.c_str());
        psl->SetDescription(description.c_str());
        hres = psl.As(&ppf);
        if (SUCCEEDED(hres))
            hres = ppf->Save(linkFileName.c_str(), TRUE);
    }
    return hres;
}

std::filesystem::path GetSysFolderLocation(int csidl)
{
    LPITEMIDLIST pidl;
    if (SUCCEEDED(::SHGetFolderLocation(NULL, csidl, NULL, 0, &pidl)))
    {
        WCHAR path[MAX_PATH];
        BOOL ret = ::SHGetPathFromIDListW(pidl, path);
        ::ILFree(pidl);
        if (ret)
            return path;
    }
    return L"";
}

std::wstring GetClassNameString(HWND hWnd)
{
    std::wstring className;
    className.resize(256);
    int nRet = ::GetClassNameW(hWnd, &className[0], className.size());
    if (nRet == 0)
        return L"";
    className.resize(nRet);
    return className;
}

std::wstring GetWindowTextString(HWND hWnd)
{
    int len = ::GetWindowTextLengthW(hWnd);
    if (len == 0)
        return L"";
    wstring caption;
    caption.resize(len + 1);
    int nRet = ::GetWindowTextW(hWnd, &caption[0], caption.size());
    if (nRet == 0)
        return L"";
    caption.resize(nRet);
    return caption;
}

std::wstring escape(const std::wstring &s)
{
    std::wstringstream wss;
    for (auto c : s)
    {
        if (c == '<' || c == '>' || c == '\\')
            wss << "\\" << c;
        else
            wss << c;
    }
    return wss.str();
}
