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

void OnStart();
void OnStop();

/*
void StartDisplayThread();
void StopDisplayThread();
void DisplayOverlay();*/

HWND hDlg;

// Key bindings
#define KEY_BIND_START_ID 1
#define KEY_BIND_STOP_ID 2

// If true, an overlay will be drawn over the application whenever clicking is true
/*bool shouldDisplayOverlay = true;

// Thread used to draw the overlay
std::thread overlayThread(DisplayOverlay);
std::atomic<bool> isOverlayThreadRunning = false;*/

// Settings used by the AutoClicker
Settings settings;

// File handler to load/save settings file
FileHandler fileHandler;

// Click handler to do the actual auto clicking
ClickHandler clickHandler;

// Process handler finds the process and window of the executable
ProcessHandler processHandler;

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

	// Create the application dialog
	CreateAppDialog(hInstance, nCmdShow);

	// Load settings from the user file
	if(fileHandler.LoadSettingsFromFile(settings))
		SendToTray();
	else
		ReturnFromTray();

	// Register the hot keys
	RegisterHotKey(hDlg, KEY_BIND_START_ID, settings.keyBindStartModifiers, settings.keyBindStart);
	RegisterHotKey(hDlg, KEY_BIND_STOP_ID, settings.keyBindStopModifiers, settings.keyBindStop);

	// Find the process
	processHandler.FindWndOfProcess(settings.exePath);

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
		if(!clickHandler.IsRunning() && mod == settings.keyBindStartModifiers && key == settings.keyBindStart)
		{
			OnStart();
		}
		else if(clickHandler.IsRunning() && mod == settings.keyBindStopModifiers && key == settings.keyBindStop)
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
	fileHandler.SaveSettingsToFile(settings);

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
	for(int i = 0; i < mbNUM_MOUSE_BTNTS; i++)
	{
		SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)mouseBtns[i]);
	}
	SendMessage(cmb, CB_SETCURSEL, settings.btn, (LPARAM)0);
}

void PullSettingsFromControls()
{
	// Get the executable path from the control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	GetWindowText(exePathEdit, settings.exePath, MAX_PATH);
	processHandler.FindWndOfProcess(settings.exePath);

	// Get the mouse button from the control
	TCHAR buffer[64];
	GetDlgItemText(hDlg, IDC_MBTN_COMBO, buffer, 63);
	for(int i = 0; i < mbNUM_MOUSE_BTNTS; i++)
	{
		if(_tcscmp(buffer, mouseBtns[i]) == 0)
		{
			settings.btn = (EMouseButton)i;
			break;
		}
	}

	// Get whether to hold or click from the control
	if(IsDlgButtonChecked(hDlg, IDC_HOLD_RADIO) == BST_CHECKED)
		settings.pressType = ptHOLD;
	else
		settings.pressType = ptCLICK;

	// Get the interval between clicks from the control
	BOOL succeeded = FALSE;
	if(int value = GetDlgItemInt(hDlg, IDC_CLICK_FREQ_EDIT, &succeeded, FALSE))
	{
		settings.timeBetweenClicksMs = value;
	}

	// Get the start hotkey from the control
	HWND startHotkey = GetDlgItem(hDlg, IDC_START_HOTKEY);
	LRESULT r1 = SendMessage(startHotkey, HKM_GETHOTKEY, 0, 0);
	settings.keyBindStart = LOBYTE(LOWORD(r1));
	settings.keyBindStartModifiers = HIBYTE(LOWORD(r1));

	// Get the stop hotkey from the control
	HWND stopHotkey = GetDlgItem(hDlg, IDC_STOP_HOTKEY);
	LRESULT r2 = SendMessage(stopHotkey, HKM_GETHOTKEY, 0, 0);
	settings.keyBindStop = LOBYTE(LOWORD(r2));
	settings.keyBindStopModifiers = HIBYTE(LOWORD(r2));

	// Unregister the old hotkeys and register the new ones
	UnregisterHotKey(hDlg, KEY_BIND_START_ID);
	UnregisterHotKey(hDlg, KEY_BIND_STOP_ID);
	RegisterHotKey(hDlg, KEY_BIND_START_ID, settings.keyBindStartModifiers, settings.keyBindStart);
	RegisterHotKey(hDlg, KEY_BIND_STOP_ID, settings.keyBindStopModifiers, settings.keyBindStop);
}

void PushSettingsToControls()
{
	// Set the executable path shown in the control
	HWND exePathEdit = GetDlgItem(hDlg, IDC_EXE_PATH_EDIT);
	SetWindowText(exePathEdit, settings.exePath);
	
	// Set the mouse button in the control
	HWND cmb = GetDlgItem(hDlg, IDC_MBTN_COMBO);
	SendMessage(cmb, CB_SETCURSEL, settings.btn, (LPARAM)0);

	// Set whether to hold or click in the control
	HWND holdBtn = GetDlgItem(hDlg, IDC_HOLD_RADIO);
	HWND clickBtn = GetDlgItem(hDlg, IDC_CLICK_RADIO);
	SendMessage(holdBtn, BM_SETCHECK, settings.pressType == ptHOLD ? BST_CHECKED : BST_UNCHECKED, NULL);
	SendMessage(clickBtn, BM_SETCHECK, settings.pressType == ptCLICK ? BST_CHECKED : BST_UNCHECKED, NULL);

	// Set the interval between clicks in the control
	SetDlgItemInt(hDlg, IDC_CLICK_FREQ_EDIT, settings.timeBetweenClicksMs, FALSE);

	// Set default start key binding
	HWND startHotkey = GetDlgItem(hDlg, IDC_START_HOTKEY);
	SendMessage(startHotkey, HKM_SETRULES, NULL, NULL);
	SendMessage(startHotkey, HKM_SETHOTKEY, MAKEWORD(settings.keyBindStart, settings.keyBindStartModifiers), 0);

	// Set the stop key binding
	HWND stopHotkey = GetDlgItem(hDlg, IDC_STOP_HOTKEY);
	SendMessage(stopHotkey, HKM_SETRULES, NULL, NULL);
	SendMessage(stopHotkey, HKM_SETHOTKEY, MAKEWORD(settings.keyBindStop, settings.keyBindStopModifiers), 0);
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

void OnStart()
{
	// If the window is unknown, try to find it
	if(!processHandler.GetWnd())
	{
		processHandler.FindWndOfProcess(settings.exePath);
	}

	// If display overlay is enabled, display the overlay thread
	/*if(hWndToInjectInto && shouldDisplayOverlay) {
		StartDisplayThread();
	}*/

	// Start the autoclicker
	clickHandler.StartAutoClicker(processHandler.GetWnd(), settings);
}

void OnStop()
{
	// If the window is unknown, try to find it
	if(!processHandler.GetWnd())
	{
		processHandler.FindWndOfProcess(settings.exePath);
	}

	// Stop the display overlay thread
	///StopDisplayThread();

	// Stop the autoclicker
	clickHandler.StopAutoClicker(processHandler.GetWnd(), settings);
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