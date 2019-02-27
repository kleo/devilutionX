#pragma once

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
// For _rotr()
#include <x86intrin.h>

// Constants
#define CONST const
#define TRUE true
#define FALSE false

#define NO_ERROR 0

// Remove calling conventions (original calling conventions confuse address-sanitizer and aren't supported by all compilers)
#define __cdecl
#define __fastcall
#define __stdcall
#define CALLBACK
#define APIENTRY
#define WINAPI
#define WINAPIV

#define ALIGNED(n) __attribute__((aligned(n)))

// Basic types
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long ALIGNED(8)

#define VOID void

typedef char CHAR;
typedef uint16_t SHORT;
typedef int32_t LONG;
typedef uint8_t BOOLEAN;

typedef LONG *PLONG;
typedef uint32_t ULONG;
typedef ULONG *PULONG;
typedef unsigned short USHORT;
typedef USHORT *PUSHORT;
typedef unsigned char UCHAR;
typedef UCHAR *PUCHAR;
typedef char *PSZ;

typedef uint32_t DWORD;
typedef int BOOL, WINBOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef float FLOAT;
typedef FLOAT *PFLOAT;
typedef BOOL *LPBOOL;
typedef BYTE *LPBYTE;
typedef int *LPINT;
typedef WORD *LPWORD;
typedef long *LPLONG;
typedef DWORD *LPDWORD;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef void *HBRUSH;
typedef void *HMENU;
typedef void *HICON;
typedef void *LPITEMIDLIST;
typedef LPITEMIDLIST PIDLIST_ABSOLUTE;
typedef LPITEMIDLIST PCIDLIST_ABSOLUTE;

typedef int INT;
typedef unsigned int UINT;
typedef unsigned int *PUINT;

// GCC qword alignment is 4, MSVC is 8, work around by introducing a more aligned type
typedef long long INT64 ALIGNED(8);
typedef unsigned long long UINT64 ALIGNED(8);

typedef intptr_t INT_PTR, *PINT_PTR;
typedef uintptr_t UINT_PTR, *PUINT_PTR;

typedef intptr_t LONG_PTR, *PLONG_PTR;
typedef uintptr_t ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR SIZE_T;

typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;

typedef CHAR *LPSTR;
typedef CHAR *LPTSTR;
typedef const CHAR *LPCSTR;

typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;

//
// Handles
//
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_HANDLE ((HANDLE)-1)
#define HFILE_ERROR ((HFILE)-1)

typedef HANDLE HWND, HGDIOBJ, HMODULE, HDC, HRGN, HINSTANCE, HPALETTE, HFILE, HCURSOR;

typedef LONG LCID;

typedef DWORD COLORREF;

typedef LONG HRESULT;

typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

//
// Intrinsics
//
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))

#define InterlockedIncrement(x) __sync_add_and_fetch(x, 1)

#define INFINITE 0xFFFFFFFF

typedef struct waveformat_tag {
	WORD wFormatTag;
	WORD nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD nBlockAlign;
} WAVEFORMAT, *PWAVEFORMAT, *LPWAVEFORMAT;

typedef struct pcmwaveformat_tag {
	WAVEFORMAT wf;
	WORD wBitsPerSample;
} PCMWAVEFORMAT, *PPCMWAVEFORMAT, *LPPCMWAVEFORMAT;

typedef struct tWAVEFORMATEX {
	WORD wFormatTag;
	WORD nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD nBlockAlign;
	WORD wBitsPerSample;
	WORD cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX, *LPCWAVEFORMATEX;

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, *LPFILETIME;

typedef struct tagRECT {
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
} RECT;

typedef RECT *LPRECT;

typedef struct tagPOINT {
	LONG x;
	LONG y;
} POINT;

typedef struct tagSIZE {
	LONG cx;
	LONG cy;
} SIZE;

typedef struct tagVS_FIXEDFILEINFO {
	DWORD dwSignature;
	DWORD dwStrucVersion;
	DWORD dwFileVersionMS;
	DWORD dwFileVersionLS;
	DWORD dwProductVersionMS;
	DWORD dwProductVersionLS;
	DWORD dwFileFlagsMask;
	DWORD dwFileFlags;
	DWORD dwFileOS;
	DWORD dwFileType;
	DWORD dwFileSubtype;
	DWORD dwFileDateMS;
	DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;

typedef struct tagMSG {
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
} MSG, *LPMSG;

#define MAKEFOURCC(x, y, z, w)             \
	(((uint32_t)((uint8_t)x))              \
	    | (((uint32_t)((uint8_t)y)) << 8)  \
	    | (((uint32_t)((uint8_t)z)) << 16) \
	    | (((uint32_t)((uint8_t)w)) << 24))

typedef uint32_t FOURCC;

typedef struct {
	FOURCC ckid;
	DWORD cksize;
	FOURCC fccType;
	DWORD dwDataOffset;
	DWORD dwFlags;
} MMCKINFO;

typedef struct tagWNDCLASSEXA {
	UINT cbSize;
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
	HICON hIconSm;
} WNDCLASSEXA;

typedef WORD ATOM;
#define WINUSERAPI
#define WNDCLASSEX WNDCLASSEXA

#define FOURCC_RIFF MAKEFOURCC('W', 'A', 'V', 'E')

//
// COM
//
#define DECLARE_INTERFACE_(name, base) struct name : public base
#define THIS_
#define THIS
#define PURE = 0

#define CS_HREDRAW 0x0001
#define CS_VREDRAW 0x0002

#define IDC_ARROW 0x1 // Dummy value

#define CSIDL_STARTMENU 0x000b

#define SW_HIDE 0
#define SW_SHOWNORMAL 1

#define BLACK_BRUSH 4

#define LR_DEFAULTCOLOR 0x0000

#define IMAGE_ICON 1

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

#define GW_HWNDNEXT 2

#define FILE_ATTRIBUTE_DIRECTORY 0x00000010

#define STDMETHOD(name) STDMETHOD_(HRESULT, name)
#define STDMETHOD_(type, name) virtual WINAPI type name

typedef void *REFIID;

struct IUnknown {
	// clang-format off
	STDMETHOD(QueryInterface)(THIS_ REFIID, LPVOID *) PURE;
	STDMETHOD_(ULONG, AddRef)(THIS) PURE;
	STDMETHOD_(ULONG, Release)(THIS) PURE;
	// clang-format on
};

#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((uint32_t)(sev) << 31) | ((uint32_t)(fac) << 16) | ((uint32_t)(code))))
#define E_FAIL ((HRESULT)0x80004005L)
#define S_OK ((HRESULT)0)

//
// Everything else
//
typedef struct tagPALETTEENTRY {
	BYTE peRed;
	BYTE peGreen;
	BYTE peBlue;
	BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

typedef void *LPTOP_LEVEL_EXCEPTION_FILTER, *PEXCEPTION_POINTERS;

typedef struct _SYSTEM_INFO {
	union {
		DWORD dwOemId;
		struct {
			WORD wProcessorArchitecture;
			WORD wReserved;
		};
	};
	DWORD dwPageSize;
	LPVOID lpMinimumApplicationAddress;
	LPVOID lpMaximumApplicationAddress;
	DWORD_PTR dwActiveProcessorMask;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwAllocationGranularity;
	WORD wProcessorLevel;
	WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef void *LPSECURITY_ATTRIBUTES;

#define ERROR_ALREADY_EXISTS 183

typedef struct _LIST_ENTRY {
	struct _LIST_ENTRY *Flink;
	struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

DWORD WINAPI GetTickCount(VOID);

DWORD WINAPI GetLastError(VOID);
VOID WINAPI SetLastError(DWORD dwErrCode);

WINBOOL WINAPI CloseHandle(HANDLE hObject);

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, WINBOOL bManualReset, WINBOOL bInitialState,
    LPCSTR lpName);
#define CreateEvent CreateEventA
BOOL WINAPI SetEvent(HANDLE hEvent);
BOOL WINAPI ResetEvent(HANDLE hEvent);
DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);

WINBOOL WINAPI SetCursorPos(int X, int Y);
int WINAPI ShowCursor(WINBOOL bShow);
HWND WINAPI SetCapture(HWND hWnd);
WINBOOL WINAPI ReleaseCapture(VOID);

SHORT WINAPI GetAsyncKeyState(int vKey);

#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001

#define WM_QUIT 0x0012

WINBOOL WINAPI PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
#define PeekMessage PeekMessageA
WINBOOL WINAPI TranslateMessage(CONST MSG *lpMsg);
LRESULT WINAPI DispatchMessageA(CONST MSG *lpMsg);
#define DispatchMessage DispatchMessageA
WINBOOL WINAPI PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define PostMessage PostMessageA

WINBOOL WINAPI DestroyWindow(HWND hWnd);
HWND WINAPI GetLastActivePopup(HWND hWnd);
HWND WINAPI GetTopWindow(HWND hWnd);
WINBOOL WINAPI SetForegroundWindow(HWND hWnd);
HWND WINAPI SetFocus(HWND hWnd);
HWND GetDesktopWindow();
HRESULT SHGetSpecialFolderLocation(HWND hwnd, int csidl, PIDLIST_ABSOLUTE *ppidl);
HWND CreateWindowExA(
    DWORD dwExStyle,
    LPCSTR lpClassName,
    LPCSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam);
#define CreateWindowEx CreateWindowExA
HWND WINAPI FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName);
#define FindWindow FindWindowA
BOOL UpdateWindow(HWND hWnd);
BOOL ShowWindow(HWND hWnd, int nCmdShow);
WINUSERAPI ATOM WINAPI RegisterClassExA(const WNDCLASSEX *lpwcx);
#define RegisterClassEx RegisterClassExA
int GetSystemMetrics(int nIndex);
HGDIOBJ GetStockObject(int i);
HCURSOR LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
#define LoadCursor LoadCursorA
HICON LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
#define LoadIcon LoadIconA
HANDLE LoadImageA(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad);
#define LoadImage LoadImageA
BOOL SHGetPathFromIDListA(PCIDLIST_ABSOLUTE pidl, LPSTR pszPath);
#define SHGetPathFromIDList SHGetPathFromIDListA
HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);
#define ShellExecute ShellExecuteA
int GetClassName(HWND hWnd, LPTSTR lpClassName, int nMaxCount);

#define THREAD_BASE_PRIORITY_MAX 2
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_HIGHEST THREAD_BASE_PRIORITY_MAX
#define THREAD_PRIORITY_ABOVE_NORMAL (THREAD_PRIORITY_HIGHEST - 1)

uintptr_t __cdecl _beginthreadex(void *_Security, unsigned _StackSize, unsigned(__stdcall *_StartAddress)(void *),
    void *_ArgList, unsigned _InitFlag, unsigned *_ThrdAddr);
HANDLE WINAPI GetCurrentThread(VOID);
DWORD WINAPI GetCurrentThreadId(VOID);
WINBOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority);
VOID WINAPI Sleep(DWORD dwMilliseconds);

VOID WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);

HDC WINAPI GetDC(HWND hWnd);
int WINAPI ReleaseDC(HWND hWnd, HDC hDC);
WINBOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c);
#define TextOut TextOutA

#define HORZRES 8
#define VERTRES 10
#define NUMRESERVED 106
int WINAPI GetDeviceCaps(HDC hdc, int index);
BOOL GetWindowRect(HWND hDlg, tagRECT *Rect);
UINT WINAPI GetSystemPaletteEntries(HDC hdc, UINT iStart, UINT cEntries, LPPALETTEENTRY pPalEntries);

int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
#define wsprintf wsprintfA
int WINAPIV wvsprintfA(LPSTR dest, LPCSTR format, va_list arglist);
#define wvsprintf wvsprintfA
int __cdecl _strcmpi(const char *_Str1, const char *_Str2);
char *__cdecl _itoa(int _Value, char *_Dest, int _Radix);

char *__cdecl _strlwr(char *str);

//
// File I/O
//
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define CREATE_ALWAYS 2
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define OPEN_EXISTING 3
#define ERROR_FILE_NOT_FOUND 2
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004

#define OFS_MAXPATHNAME 128
#define MAX_PATH 260

typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	CHAR cFileName[MAX_PATH];
	CHAR cAlternateFileName[14];
	DWORD dwFileType;
	DWORD dwCreatorType;
	WORD wFinderFlags;
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

typedef void *LPOVERLAPPED;

typedef BOOL(CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct _OFSTRUCT {
	BYTE cBytes;
	BYTE fFixedDisk;
	WORD nErrCode;
	WORD Reserved1;
	WORD Reserved2;
	CHAR szPathName[OFS_MAXPATHNAME];
} OFSTRUCT, *LPOFSTRUCT, *POFSTRUCT;

#define VER_PLATFORM_WIN32_NT 2

typedef struct _OSVERSIONINFOA {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  CHAR  szCSDVersion[128];
} OSVERSIONINFO, *LPOSVERSIONINFOA;

BOOL GetVersionExA(LPOSVERSIONINFOA lpVersionInformation);
#define GetVersionEx GetVersionExA

#define SEC_COMMIT 0x8000000
#define PAGE_READWRITE 0x04

#define FILE_MAP_ALL_ACCESS SECTION_ALL_ACCESS
#define SECTION_QUERY 0x0001
#define SECTION_MAP_WRITE 0x0002
#define SECTION_MAP_READ 0x0004
#define SECTION_MAP_EXECUTE 0x0008
#define SECTION_EXTEND_SIZE 0x0010
#define SECTION_MAP_EXECUTE_EXPLICIT 0x0020
#define STANDARD_RIGHTS_REQUIRED 0x000F0000

#define SECTION_ALL_ACCESS \
	(STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_WRITE | SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_EXTEND_SIZE)

#define CREATE_NEW_PROCESS_GROUP 0x200

typedef struct _PROCESS_INFORMATION {
	HANDLE hProcess;
	HANDLE hThread;
	DWORD dwProcessId;
	DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef void *LPSTARTUPINFOA;
WINBOOL WINAPI CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes, WINBOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation);
#define CreateProcess CreateProcessA
VOID WINAPI ExitProcess(UINT uExitCode);
DWORD WINAPI GetCurrentProcessId(VOID);

HANDLE WINAPI CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect,
    DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName);
#define CreateFileMapping CreateFileMappingA
LPVOID WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh,
    DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap);
WINBOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress);

DWORD WINAPI WaitForInputIdle(HANDLE hProcess, DWORD dwMilliseconds);
HWND WINAPI GetForegroundWindow(VOID);
HWND WINAPI GetWindow(HWND hWnd, UINT uCmd);
DWORD WINAPI GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId);

DWORD WINAPI GetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString,
    DWORD nSize, LPCSTR lpFileName);
#define GetPrivateProfileString GetPrivateProfileStringA
int MessageBoxA(HWND hWnd, const char *Text, const char *Title, UINT Flags);
#define MessageBox MessageBoxA
typedef LONG LSTATUS, HKEY, REGSAM, PHKEY;
#define HKEY_CURRENT_USER 1
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define REG_SZ 1
LSTATUS RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
#define RegOpenKeyEx RegOpenKeyExA
LSTATUS RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, BYTE *lpData, LPDWORD lpcbData);
#define RegQueryValueEx RegQueryValueExA
LSTATUS RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
#define RegSetValueEx RegSetValueExA
LSTATUS RegCloseKeyA(HKEY hKey);
#define RegCloseKey RegCloseKeyA
void PostQuitMessage(int nExitCode);
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define DefWindowProc DefWindowProcA
LONG GetWindowLongA(HWND hWnd, int nIndex);
#define GetWindowLong GetWindowLongA
LONG SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
#define SetWindowLong SetWindowLongA

WINBOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped);
DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
WINBOOL WINAPI SetEndOfFile(HANDLE hFile);
DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName);
#define GetFileAttributes GetFileAttributesA
WINBOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes);
#define SetFileAttributes SetFileAttributesA
HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
#define FindFirstFile FindFirstFileA
BOOL FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
#define FindNextFile FindNextFileA
WINBOOL WINAPI FindClose(HANDLE hFindFile);
HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
#define CreateFile CreateFileA
WINBOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped);
DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
UINT WINAPI GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize);
#define GetWindowsDirectory GetWindowsDirectoryA
DWORD GetCurrentDirectory(DWORD nBufferLength, LPTSTR lpBuffer);
DWORD GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer);
#define GetLogicalDriveStrings GetLogicalDriveStringsA
UINT GetDriveTypeA(LPCSTR lpRootPathName);
#define GetDriveType GetDriveTypeA
WINBOOL WINAPI GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector,
    LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters);
#define GetDiskFreeSpace GetDiskFreeSpaceA
DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
#define GetModuleFileName GetModuleFileNameA
WINBOOL WINAPI GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize);
#define GetComputerName GetComputerNameA
DWORD GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
#define GetFileVersionInfoSize GetFileVersionInfoSizeA
BOOL GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
#define GetFileVersionInfo GetFileVersionInfoA
BOOL VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);
#define VerQueryValue VerQueryValueA
WINBOOL WINAPI DeleteFileA(LPCSTR lpFileName);
#define DeleteFile DeleteFileA
WINBOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, WINBOOL bFailIfExists);
#define CopyFile CopyFileA
HFILE WINAPI OpenFile(LPCSTR lpFileName, LPOFSTRUCT lpReOpenBuff, UINT uStyle);

#define GWL_STYLE (-16)

#define WS_POPUP 0x80000000L
#define WS_SYSMENU 0x00080000L

#define DRIVE_CDROM 5

//
// Events
//
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205

#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104

#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112

#define WM_CHAR 0x0102
#define WM_CAPTURECHANGED 0x0215

#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_ACTIVATEAPP 0x001C
#define WM_QUERYNEWPALETTE 0x030F
#define WM_PALETTECHANGED 0x0311

#define SC_CLOSE 0xF060

#define VK_RETURN 0x0D
#define VK_BACK 0x08
#define VK_SHIFT 0x10
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28

#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B

#define VK_TAB 0x09
#define VK_PAUSE 0x13
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_SNAPSHOT 0x2C

#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
//#define VK_OEM_8 0xDF
//#define VK_OEM_102 0xE2

#define MK_SHIFT 0x0004
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002

#define MB_TASKMODAL 0x00002000L
#define MB_ICONHAND 0x00000010L
#define MB_ICONEXCLAMATION 0x00000030L

#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000

#define HWND_NOTOPMOST (HWND) - 2
#define HWND_TOP (HWND)0

#define SWP_NOACTIVATE 0x0010
#define SWP_NOMOVE 0x0002
#define SWP_NOSIZE 0x0004
#define SWP_NOZORDER 0x0001

//
// Total fakes
//
typedef struct {
} SOCKADDR, GUID, *LPGUID;

typedef struct {
	DWORD cb;
} STARTUPINFOA;

//
// MSCVRT emulation
//

extern void LoadCharNames();
extern void LoadAndPlaySound(char *FilePath, int lVolume, int lPan);
extern void DrawArtWithMask(int SX, int SY, int SW, int SH, int nFrame, BYTE bMask, void *pBuffer);
extern BOOL __cdecl LoadArtWithPal(char *pszFile, void **pBuffer, int frames, DWORD *data);

#define OF_EXIST 1

#include "miniwin_ddraw.h"
#include "miniwin_dsound.h"
#include "miniwin_ui.h"
#include "miniwin_thread.h"
#include "miniwin_rand.h"
