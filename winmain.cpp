#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winsock.h>
#include <vfw.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <io.h>

#include "voice.h"
#include "audio.h"
#include "queue.h"
#include "types.h"

#include "zoomy.h"
#include "sock.h"

// defines
#define WMU_CAPTURE WM_USER + 1

// prototypes
LRESULT CALLBACK WinProc(HWND, UINT, WPARAM, LPARAM);
void RedirectIOToConsole(int debug);
char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
void getBitmapFromWindow(HWND hwnd, int startx, int starty, int width, int height, unsigned char *data);







int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	HWND hwnd;
	MSG msg;
	char szAppName[] = TEXT("zoomy");

	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WinProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(GetModuleHandle(NULL), IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = szAppName;

	RegisterClass(&wc);

	// Create the window
	hwnd = CreateWindow(szAppName, szAppName,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, hInstance, 0);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	while (TRUE)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			if (GetMessage(&msg, NULL, 0, 0) > 0)
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				break;
			}
		}
		else
		{
			SendMessage(hwnd, WMU_CAPTURE, 0, 0);
		}
	}
	return msg.wParam;
}


LRESULT CALLBACK WinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static WSADATA	WSAData;
	static HWND		camhwnd = NULL;
	void *voice_data = NULL;
	HANDLE hThread;
	static Zoomy zoomy;

	switch (message)
	{
	case WM_CREATE:
	{
		SetTimer(hwnd, 0, 500, NULL);

		WSAStartup(MAKEWORD(2, 0), &WSAData);
		RedirectIOToConsole(true);
		zoomy.init(hwnd, NULL);
		hThread = CreateThread(NULL, 0, Zoomy::VoiceThread, &voice_data, 0, NULL);
		break;
	}
	case WM_KEYDOWN:
		zoomy.keydown(wParam);
		break;
	case WMU_CAPTURE:
	{
		zoomy.capture();
		break;
	}
	case WM_TIMER:
	{
		zoomy.step();
		break;
	}
	case WM_SIZE:
	{
		zoomy.resize(LOWORD(lParam), HIWORD(lParam));
		break;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC		hdc;

		hdc = BeginPaint(hwnd, &ps);

		zoomy.paint(hdc);

		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_DESTROY:
	{
		if (Zoomy::enable_video)
		{
			SendMessage(camhwnd, WM_CAP_DRIVER_DISCONNECT, 0, 0);
		}
		PostQuitMessage(0);
		break;
	}

	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}



void getBitmapFromWindow(HWND hwnd, int startx, int starty, int width, int height, unsigned char *data)
{
	HDC hdc = GetDC(hwnd);
	HDC hTargetDC = CreateCompatibleDC(hdc);
	RECT rect = { startx, starty, width, height };


	HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
	SelectObject(hTargetDC, hBitmap);
	PrintWindow(hwnd, hTargetDC, PW_CLIENTONLY);

	GetBitmapBits(hBitmap, FRAME_SIZE, data);

	DeleteObject(hBitmap);
	ReleaseDC(hwnd, hdc);
	DeleteDC(hTargetDC);
}


