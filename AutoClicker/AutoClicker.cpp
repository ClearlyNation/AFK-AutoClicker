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

void InitControls();
void PullSettingsFromControls();
void PushSettingsToControls();

void SendToTray();
void ReturnFromTray();

void SaveSettingsToFile();
void LoadSettingsFromFile();

void GetSettingsFilePath(TCHAR ** path);

bool GetProcess(TCHAR path[MAX_PATH]);

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

HWND hDlg;
HWND hWndToInjectInto;
HHOOK hhkLowLevelKybd;
HHOOK hhkLowLevelMouse;

// Key bindings
DWORD keyBindStart = VK_F9;
DWORD keyBindStop = VK_F9;

// Path of the executable to inject input into
TCHAR exePath[MAX_PATH];

// True iff the auto clicker is clicking
std::atomic<bool> clicking = false;

// ID of the mouse button to simulate
// 0 = l, 1 = r, 2 = m
DWORD mouseToClick = 1;

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

	// Get the default process
	///GetProcess(exePath);

	// Create the application dialog
	CreateAppDialog(hInstance, nCmdShow);

	// Install the low-level keyboard & mouse hooks
	///hhkLowLevelMouse = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, 0, 0);	// TODO - If re-enabled, add cleanup code
	hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);

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
	ShowWindow(hDlg, nCmdShow);
	///SendToTray();
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HMENU popup;
	POINT cursorPos;

	switch(uMsg)
	{
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

void InitControls()
{
	if(controlsInitialised)
		return;

	controlsInitialised = true;

	// Set the path in the edit path control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	SetWindowText(exePathEdit, exePath);

	// Set the default click option to hold
	HWND clickRadio = GetDlgItem(hDlg, IDC_CLICK_RADIO);
	SendMessage(clickRadio, BM_SETCHECK, BST_CHECKED, 1);

	// Set the default start key binding
	HWND startEdit = GetDlgItem(hDlg, IDC_START_EDIT);
	TCHAR keyName[64];
	if(GetKeyNameText(keyBindStart, keyName, 64))
	{
		SetWindowText(startEdit, keyName);
	}

	// Set the default stop key binding
	HWND stopEdit = GetDlgItem(hDlg, IDC_STOP_EDIT);
	if(GetKeyNameText(keyBindStop, keyName, 64))
	{
		SetWindowText(stopEdit, keyName);
	}

	// Get the combo box
	HWND cmb = GetDlgItem(hDlg, IDC_MBTN_COMBO);

	// Add items to the combo box
	TCHAR lbtn[] = L"Left Mouse Button";
	TCHAR rbtn[] = L"Right Mouse Button";
	TCHAR mbtn[] = L"Middle Mouse Button";
	SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)lbtn);
	int index = SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)rbtn);
	SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)mbtn);
	SendMessage(cmb, CB_SETCURSEL, index, (LPARAM)0);
}

void PullSettingsFromControls()
{
	// Get the executable path from the control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	GetWindowText(exePathEdit, exePath, MAX_PATH);
	GetProcess(exePath);
}

void PushSettingsToControls()
{
	// Set the executable path shown in the control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	SetWindowText(exePathEdit, exePath);
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
}

void LoadSettingsFromFile()
{
	// Get the path of the settings file
	TCHAR * path;
	GetSettingsFilePath(&path);
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

void OnStartClicking(DWORD btn)
{
	while(clicking)
	{
		SendMessage(hWndToInjectInto, btn, 100, 100);
		Sleep(10);
	}
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0)
	{
		switch(wParam)
		{
		// If the mouse has been released when it should be held down, put it back down
		case WM_LBUTTONUP:
			if(mouseToClick == 0 && clicking)
			{
				SendMessage(hWndToInjectInto, WM_LBUTTONDOWN, 100, 100);
			}
			break;
		case WM_RBUTTONUP:
			if(mouseToClick == 0 && clicking)
			{
				SendMessage(hWndToInjectInto, WM_RBUTTONDOWN, 100, 100);
			}
			break;
		case WM_MBUTTONUP:
			if(mouseToClick == 0 && clicking)
			{
				SendMessage(hWndToInjectInto, WM_MBUTTONDOWN, 100, 100);
			}
			break;
		}
	}
	return CallNextHookEx(hhkLowLevelMouse, nCode, wParam, lParam);
}

// https://stackoverflow.com/questions/22975916/global-keyboard-hook-with-wh-keyboard-ll-and-keybd-event-windows
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	BOOL fEatKeystroke = FALSE;

	if (nCode == HC_ACTION)
	{
		switch (wParam)
		{
		///case WM_KEYDOWN:
		///case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;
			// Redirect a to b
			if (!clicking && (fEatKeystroke = (p->vkCode == keyBindStart)))
			{
				if(hWndToInjectInto)
				{
					clicking = true;
					DWORD btn = mouseToClick == 0 ? WM_LBUTTONDOWN : mouseToClick == 1 ? WM_RBUTTONDOWN : WM_MBUTTONDOWN;
					// Holding down won't work because tabbing out cause the mouseup event to trigger
					// Send a mouse down event every second so if a mouseup occurs, there will be little downtime
					std::thread t([](DWORD btn)
					{
						while(clicking)
						{
							SendMessage(hWndToInjectInto, btn, 100, 100);
							Sleep(1000);
						}
					}, btn);
					t.detach();
				}
				else
				{
					MessageBox(NULL, _T("Window could not be found for specified executable"), NULL, NULL);
				}
				break;
			}
			else if (clicking && (fEatKeystroke = (p->vkCode == keyBindStop)))
			{
				if(hWndToInjectInto)
				{
					clicking = false;
					DWORD btn = mouseToClick == 0 ? WM_LBUTTONUP : mouseToClick == 1 ? WM_RBUTTONUP : WM_MBUTTONUP;
					SendMessage(hWndToInjectInto, WM_RBUTTONUP, 100, 100);
				}
				else
				{
					MessageBox(NULL, _T("Window could not be found for specified executable"), NULL, NULL);
				}
				break;
			}
			break;
		}
	}
	return (fEatKeystroke ? 1 : CallNextHookEx(NULL, nCode, wParam, lParam));
}
