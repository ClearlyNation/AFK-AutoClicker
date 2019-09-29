// AutoClicker.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "AutoClicker.h"

#pragma comment(linker, \
  "\"/manifestdependency:type='Win32' "\
  "name='Microsoft.Windows.Common-Controls' "\
  "version='6.0.0.0' "\
  "processorArchitecture='*' "\
  "publicKeyToken='6595b64144ccf1df' "\
  "language='*'\"")

#pragma comment(lib, "ComCtl32.lib")

#define MAX_LOADSTRING 100
#define IDM_TRAY (WM_APP + 1)
#define IDM_OPEN_DIALOG (WM_APP + 2)
#define IDM_CLOSE_APP (WM_APP + 3)

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateAppDialog(HINSTANCE hInst, int nCmdShow);
void OnOkClicked();
void OnCancelClicked();
void OnExePathBrowseClicked();

void InitControls();
void PullSettingsFromControls();
void PushSettingsToControls();

void SendToTray();
void ReturnFromTray();

void SaveSettingsToFile();
void LoadSettingsFromFile();

void GetSettingsFilePath(TCHAR ** path);

bool GetProcess(TCHAR path[MAX_PATH]);

void OnStart();
void OnStop();
/*
void StartDisplayThread();
void StopDisplayThread();
void DisplayOverlay();*/

HWND hDlg;
HWND hWndToInjectInto;
HHOOK hhkLowLevelKybd;
HHOOK hhkLowLevelMouse;

// Key bindings
#define KEY_BIND_START_ID 1
#define KEY_BIND_STOP_ID 2
DWORD keyBindStart = VK_F9;
DWORD keyBindStartModifiers = NULL;
DWORD keyBindStop = VK_F9;
DWORD keyBindStopModifiers = NULL;

// Path of the executable to inject input into
TCHAR exePath[MAX_PATH];

// True iff the auto clicker is clicking
std::atomic<bool> clicking = false;

// ID of the mouse button to simulate
// 0 = l, 1 = r, 2 = m
#define NUM_MOUSE_BTNS 3
DWORD mouseToClick = 0;
TCHAR const * mouseBtns[NUM_MOUSE_BTNS] = {
	_T("Left Mouse Button"),
	_T("Right Mouse Button"),
	_T("Middle Mouse Button")
};

// Whether to click or hold
// 0 = hold, 1 = click
// NOTE - Hold does not actually simulate holding the mouse, depending on the application it made be registered as a click instead of a hold
// TODO - Fix above
DWORD holdOrClick = 1;

// Time between clicks
DWORD timeBetweenClicksMs = 1000;

// If true, an overlay will be drawn over the application whenever clicking is true
/*bool shouldDisplayOverlay = true;

// Thread used to draw the overlay
std::thread overlayThread(DisplayOverlay);
std::atomic<bool> isOverlayThreadRunning = false;*/

// True iff the controls have not been initialised
// Controls only need to be initialised once
bool controlsInitialised = false;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	// Load settings from the user file
	LoadSettingsFromFile();

	// Create the application dialog
	CreateAppDialog(hInstance, nCmdShow);

	// Register the hot keys
	RegisterHotKey(hDlg, KEY_BIND_START_ID, keyBindStartModifiers, keyBindStart);
	RegisterHotKey(hDlg, KEY_BIND_STOP_ID, keyBindStopModifiers, keyBindStop);

	// Find the process
	GetProcess(exePath);

	// Listen for events
	BOOL ret;
	MSG msg;
	while((ret = GetMessage(&msg, 0, 0, 0)) != 0) {
		if(ret == -1) /* error found */
			return -1;

		if(!IsDialogMessage(hDlg, &msg)) {
			TranslateMessage(&msg); /* translate virtual-key messages */
			DispatchMessage(&msg); /* send it to dialog procedure */
		}
	}

    return (int) msg.wParam;
}

void CreateAppDialog(HINSTANCE hInst, int nCmdShow)
{
	hDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_SETTINGS), 0, DialogProc, 0);
	SendToTray();
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HMENU popup;
	POINT cursorPos;

	switch(uMsg)
	{
	// WM_INITDIALOG seems to be called before dlg items are made available through GetDlgItem
	///case WM_INITDIALOG:
	///	InitControls();
	///	break;
	case WM_CLOSE:
		OnOkClicked();	// Pretend pressing the x button is pressing ok
		///DestroyWindow(hDlg);
		break;
	case WM_DESTROY:
		// TODO - Place any cleanup code here
		UnhookWindowsHookEx(hhkLowLevelKybd);
		EndDialog(hDlg, 0);
		PostQuitMessage(0);
		return TRUE;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			OnCancelClicked();
			///SendMessage(hDlg, WM_CLOSE, 0, 0);
			break;
		case IDOK:
			OnOkClicked();
			break;
		case IDM_CLOSE_APP:
			DestroyWindow(hDlg);
			break;
		case IDM_OPEN_DIALOG:
			ReturnFromTray();
			break;
		case IDC_EXE_PATH_BROWSE:
			OnExePathBrowseClicked();
			break;
		}
		break;
	case IDM_TRAY:
		switch(lParam)
		{
		case WM_LBUTTONUP:
			ReturnFromTray();
			break;
		case WM_RBUTTONUP:
			// Add a context menu
			GetCursorPos(&cursorPos);
			popup = CreatePopupMenu();
			InsertMenu(popup, 0, MF_BYPOSITION | MF_STRING, IDM_CLOSE_APP, L"Quit");
			InsertMenu(popup, 0, MF_BYPOSITION | MF_STRING, IDM_OPEN_DIALOG, L"Open Settings");
			TrackPopupMenu(popup, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursorPos.x, cursorPos.y, 0, hDlg, NULL);
			break;
		}
		break;
	case WM_HOTKEY:
		DWORD mod = LOWORD(lParam);
		DWORD key = HIWORD(lParam);
		if(!clicking && mod == keyBindStartModifiers && key == keyBindStart)
		{
			OnStart();
		}
		else if(clicking && mod == keyBindStopModifiers && key == keyBindStop)
		{
			OnStop();
		}
	}
	return FALSE;
}

void OnCancelClicked()
{
	// Push settings from settings variables to gui
	PushSettingsToControls();

	// Hide dialog and show the system tray icon
	SendToTray();
}

void OnOkClicked()
{
	// Pull settings from gui to the settings variables
	PullSettingsFromControls();

	// Save changes to the settings file
	SaveSettingsToFile();

	// Hide dialog and show the system tray icon
	SendToTray();
}

void OnExePathBrowseClicked()
{
	TCHAR szFile[MAX_PATH];
	TCHAR szInitialFolder[MAX_PATH];

	// Get the initial file/folder
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	GetWindowText(exePathEdit, szInitialFolder, MAX_PATH);

	// Setup file browser settings
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hDlg;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	///ofn.lpstrFilter = L"All\0*.*ALL\0";
	///ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = szInitialFolder;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	// Open the file browser
	GetOpenFileName(&ofn);

	// Set the filename in the edit text
	exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	SetWindowText(exePathEdit, ofn.lpstrFile);
}

void InitControls()
{
	if(controlsInitialised)
		return;

	controlsInitialised = true;

	// Set the title of the dialog
	SetWindowText(hDlg, _T("AutoClicker Settings"));

	// Push current settings to the dialog controls
	PushSettingsToControls();

	// Get the combo box
	HWND cmb = GetDlgItem(hDlg, IDC_MBTN_COMBO);

	// Add items to the combo box
	for(int i = 0; i < NUM_MOUSE_BTNS; i++)
	{
		SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)mouseBtns[i]);
	}
	SendMessage(cmb, CB_SETCURSEL, mouseToClick, (LPARAM)0);
}

void PullSettingsFromControls()
{
	// Get the executable path from the control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	GetWindowText(exePathEdit, exePath, MAX_PATH);
	GetProcess(exePath);

	// Get the mouse button from the control
	TCHAR buffer[64];
	GetDlgItemText(hDlg, IDC_MBTN_COMBO, buffer, 63);
	for(int i = 0; i < NUM_MOUSE_BTNS; i++)
	{
		if(_tcscmp(buffer, mouseBtns[i]) == 0)
		{
			mouseToClick = i;
			break;
		}
	}

	// Get whether to hold or click from the control
	if(IsDlgButtonChecked(hDlg, IDC_HOLD_RADIO) == BST_CHECKED)
		holdOrClick = 0;
	else
		holdOrClick = 1;

	// Get the interval between clicks from the control
	BOOL succeeded = FALSE;
	if(int value = GetDlgItemInt(hDlg, IDC_CLICK_FREQ_EDIT, &succeeded, FALSE))
	{
		timeBetweenClicksMs = value;
	}

	// Get the start hotkey from the control
	HWND startHotkey = GetDlgItem(hDlg, IDC_START_HOTKEY);
	LRESULT r1 = SendMessage(startHotkey, HKM_GETHOTKEY, 0, 0);
	keyBindStart = LOBYTE(LOWORD(r1));
	keyBindStartModifiers = HIBYTE(LOWORD(r1));

	// Get the stop hotkey from the control
	HWND stopHotkey = GetDlgItem(hDlg, IDC_STOP_HOTKEY);
	LRESULT r2 = SendMessage(stopHotkey, HKM_GETHOTKEY, 0, 0);
	keyBindStop = LOBYTE(LOWORD(r2));
	keyBindStopModifiers = HIBYTE(LOWORD(r2));

	// Unregister the old hotkeys and register the new ones
	UnregisterHotKey(hDlg, KEY_BIND_START_ID);
	UnregisterHotKey(hDlg, KEY_BIND_STOP_ID);
	RegisterHotKey(hDlg, KEY_BIND_START_ID, keyBindStartModifiers, keyBindStart);
	RegisterHotKey(hDlg, KEY_BIND_STOP_ID, keyBindStopModifiers, keyBindStop);
}

void PushSettingsToControls()
{
	// Set the executable path shown in the control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	SetWindowText(exePathEdit, exePath);
	
	// Set the mouse button in the control
	HWND cmb = GetDlgItem(hDlg, IDC_MBTN_COMBO);
	SendMessage(cmb, CB_SETCURSEL, mouseToClick, (LPARAM)0);

	// Set whether to hold or click in the control
	HWND holdBtn = GetDlgItem(hDlg, IDC_HOLD_RADIO);
	HWND clickBtn = GetDlgItem(hDlg, IDC_CLICK_RADIO);
	SendMessage(holdBtn, BM_SETCHECK, holdOrClick == 0 ? BST_CHECKED : BST_UNCHECKED, NULL);
	SendMessage(clickBtn, BM_SETCHECK, holdOrClick == 1 ? BST_CHECKED : BST_UNCHECKED, NULL);

	// Set the interval between clicks in the control
	SetDlgItemInt(hDlg, IDC_CLICK_FREQ_EDIT, timeBetweenClicksMs, FALSE);

	// Set default start key binding
	HWND startHotkey = GetDlgItem(hDlg, IDC_START_HOTKEY);
	SendMessage(startHotkey, HKM_SETRULES, NULL, NULL);
	SendMessage(startHotkey, HKM_SETHOTKEY, MAKEWORD(keyBindStart, keyBindStartModifiers), 0);

	// Set the stop key binding
	HWND stopHotkey = GetDlgItem(hDlg, IDC_STOP_HOTKEY);
	SendMessage(stopHotkey, HKM_SETRULES, NULL, NULL);
	SendMessage(stopHotkey, HKM_SETHOTKEY, MAKEWORD(keyBindStop, keyBindStopModifiers), 0);
}

void SendToTray()
{
	// Create and show a system tray icon
	NOTIFYICONDATA nid;
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hDlg;
	nid.uID = 100;
	nid.uVersion = NOTIFYICON_VERSION;
	nid.uCallbackMessage = IDM_TRAY;
	nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcscpy_s(nid.szTip, L"AutoClicker");
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	Shell_NotifyIcon(NIM_ADD, &nid);

	// Hide the dialog
	ShowWindow(hDlg, SW_HIDE);
}

void ReturnFromTray()
{
	InitControls();
	NOTIFYICONDATA nid;
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hDlg;
	nid.uID = 100;
	Shell_NotifyIcon(NIM_DELETE, &nid);
	ShowWindow(hDlg, SW_SHOW);
}

void SaveSettingsToFile()
{
	// Get the path of the settings file
	TCHAR * path;
	GetSettingsFilePath(&path);

	// Open and write to the file
	std::wofstream f;
	f.open(path);
	f << L"EXEPATH " << exePath << L"\n";
	f << L"HOLDORCLICK " << holdOrClick << L"\n";
	f << L"MBTN " << mouseToClick << L"\n";
	f << L"INTERVAL " << timeBetweenClicksMs << L"\n";
	f << L"KEYSTART " << keyBindStart << L"\n";
	f << L"KEYSTARTMOD " << keyBindStartModifiers << L"\n";
	f << L"KEYSTOP " << keyBindStop << L"\n";
	f << L"KEYSTOPMOD " << keyBindStopModifiers << L"\n";
	f.close();
}

void LoadSettingsFromFile()
{
	// Get the path of the settings file
	TCHAR * path;
	GetSettingsFilePath(&path);
	
	std::wifstream f;
	f.open(path);
	std::wstring line;
	while(std::getline(f, line))
	{
		// NOTE - Only works if TCHAR is a wide char
		std::wstring key;
		std::wstring value;
		size_t splitPos = line.find(' ');
		key = line.substr(0, splitPos);
		value = line.substr(splitPos+1, line.size()-splitPos-1);

		// Determine which variable to set based on key name
		if(key == L"EXEPATH")
		{
			memcpy_s(exePath, MAX_PATH*sizeof(TCHAR), value.c_str(), value.size()*sizeof(wchar_t));
		}
		else if(key == L"HOLDORCLICK")
		{
			holdOrClick = wcstol(value.c_str(), NULL, 10);
		}
		else if(key == L"MBTN")
		{
			mouseToClick = wcstol(value.c_str(), NULL, 10);
		}
		else if(key == L"INTERVAL")
		{
			timeBetweenClicksMs = wcstol(value.c_str(), NULL, 10);
		}
		else if(key == L"KEYSTART")
		{
			keyBindStart = wcstol(value.c_str(), NULL, 10);
		}
		else if(key == L"KEYSTARTMOD")
		{
			keyBindStartModifiers = wcstol(value.c_str(), NULL, 10);
		}
		else if(key == L"KEYSTOP")
		{
			keyBindStop = wcstol(value.c_str(), NULL, 10);
		}
		else if(key == L"KEYSTOPMOD")
		{
			keyBindStopModifiers = wcstol(value.c_str(), NULL, 10);
		}
	}
}

void GetSettingsFilePath(TCHAR ** path)
{
	TCHAR szPath[MAX_PATH];
	// Get the AppData/Roaming folder
	if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, szPath)))
	{
		// Append the application folder
		PathAppend(szPath, _T("\\Origami Sheep\\AutoClicker\\"));

		// Create the folders if they don't already exist
		SHCreateDirectoryEx(NULL, szPath, NULL);

		// Append the filename
		PathAppend(szPath, _T("\\Settings.txt"));
		*path = szPath;
	}
}

// Called as windows are enumertated over
BOOL CALLBACK EnumWindowsProc(HWND hwnd,LPARAM lParam)
{
	DWORD lpdwProcessId;
	GetWindowThreadProcessId(hwnd, &lpdwProcessId);
	if(lpdwProcessId == lParam)
	{
	///	MessageBox(NULL, _T("Found Window"), _T("Found Window"), NULL);
		TCHAR title[256];
		GetWindowText(hwnd, title, sizeof(title));
	///	MessageBox(NULL, title, title, NULL);
		hWndToInjectInto = hwnd;
		return FALSE;
	}
	return TRUE;
}

// Get the process executed from the specified path
// Returns true iff the process is retrieved
// https://docs.microsoft.com/en-us/windows/win32/psapi/enumerating-all-processes
bool GetProcess(TCHAR path[MAX_PATH])
{
	DWORD aProcesses[1024], cbNeeded, cProcesses;

	if(!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
	{
		return false;
	}

	// Calculate how many process identifiers were returned
	cProcesses = cbNeeded / sizeof(DWORD);

	// Find the process required
	for(DWORD i = 0; i < cProcesses; i++)
	{
		if(aProcesses[i] != 0)
		{
			// Get a handle to the process
			DWORD pid = aProcesses[i];
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

			if(hProcess)
			{
				// Get the path of the process
				TCHAR filename[MAX_PATH];
				DWORD filenameLen;
				if((filenameLen = GetModuleFileNameEx(hProcess, NULL, filename, MAX_PATH)) >= 0)
				{
					// Check if the path is the same as the path specified
					if(_tcscmp(filename, path) == 0)
					{
						EnumWindows(EnumWindowsProc, pid);
						return true;
					}
				}
			}

			// Close the handle to the process
			CloseHandle(hProcess);
		}
	}

	return false;
}

void OnStart()
{
	// If the window is unknown, try to find it
	if(!hWndToInjectInto)
	{
		GetProcess(exePath);
	}

	// Ensure the window is know before trying to inject input
	if(hWndToInjectInto)
	{
		// If display overlay is enabled, display the overlay thread
		/*if(shouldDisplayOverlay) {
			StartDisplayThread();
		}*/

		clicking = true;
		DWORD btnDown = mouseToClick == 0 ? WM_LBUTTONDOWN : mouseToClick == 1 ? WM_RBUTTONDOWN : WM_MBUTTONDOWN;
		DWORD btnUp = mouseToClick == 0 ? WM_LBUTTONUP : mouseToClick == 1 ? WM_RBUTTONUP : WM_MBUTTONUP;
		// Holding down won't work because tabbing out cause the mouseup event to trigger
		// Send a mouse down event every second so if a mouseup occurs, there will be little downtime
		std::thread t([ btnDown, btnUp ]()
		{
			while(clicking)
			{
				SendMessage(hWndToInjectInto, btnDown, 100, 100);
				Sleep(timeBetweenClicksMs);
				if(holdOrClick == 1) {
					// Click requires a press followed by a release
					SendMessage(hWndToInjectInto, btnUp, 100, 100);
				}
			}
		});
		t.detach();
	}
	else
	{
		MessageBox(NULL, _T("Window could not be found for specified executable"), NULL, NULL);
	}
}

void OnStop()
{
	// If the window is unknown, try to find it
	if(!hWndToInjectInto)
	{
		GetProcess(exePath);
	}

	// Stop the display overlay thread
	///StopDisplayThread();

	// Ensure the window is know before trying to inject input
	if(hWndToInjectInto)
	{
		clicking = false;
		DWORD btn = mouseToClick == 0 ? WM_LBUTTONUP : mouseToClick == 1 ? WM_RBUTTONUP : WM_MBUTTONUP;
		SendMessage(hWndToInjectInto, btn, 100, 100);
	}
	else
	{
		MessageBox(NULL, _T("Window could not be found for specified executable"), NULL, NULL);
	}
}
/*
void StartDisplayThread()
{
	if(!isOverlayThreadRunning)
	{
		isOverlayThreadRunning = true;
		overlayThread.detach();
	}
}

void StopDisplayThread()
{
	if(isOverlayThreadRunning)
	{
		isOverlayThreadRunning = false;
		overlayThread.join();
	}
}

void DisplayOverlay()
{
	if(hWndToInjectInto) {
		while(isOverlayThreadRunning) {
			HDC dc = GetDC(hWndToInjectInto);
			if(dc) {
				RECT r;
				GetClientRect(hWndToInjectInto, &r);
				if(clicking) {
					DrawText(dc, L"Auto Clicker Active", -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
				} else {
					DrawText(dc, L"Auto Clicker Inactive", -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
				}
				ReleaseDC(hWndToInjectInto, dc);
			}
			Sleep(50);
		}
	}
}
*/