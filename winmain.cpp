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

// defines
#define WMU_CAPTURE WM_USER + 1
#define WIDTH 320
#define HEIGHT 240
#define FRAME_SIZE (WIDTH * HEIGHT * 4)

#define DISPLAY_WIDTH 640
#define DISPLAY_HEIGHT 480

// prototypes
LRESULT CALLBACK WinProc(HWND, UINT, WPARAM, LPARAM);
int listen_socket(int &sock, unsigned short port);
LRESULT frameCallback(HWND hWnd, LPVIDEOHDR lpVHdr);
void yuy2_to_rgb(unsigned char *yuvData, COLORREF *data);
void draw_pixels(HDC hdc, int xoff, int yoff, int width, int height, int scalew, int scaleh, unsigned char *data);
void handle_accepted(int &csock, char *buffer, int &size);
int connect_socket(char *ip_addr, unsigned short port, int &sock);
void RedirectIOToConsole(int debug);
char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int set_sock_options(int sock);
void handle_listen(int &sock, int &csock);
void getBitmapFromWindow(HWND hwnd, int startx, int starty, int width, int height, unsigned char *data);


// globals (Large buffers cant be on stack)
queue_t squeue;
queue_t rqueue;

unsigned char blue_check[FRAME_SIZE];
unsigned char red_check[FRAME_SIZE];
unsigned char grey_check[FRAME_SIZE];


unsigned char rbuffer[FRAME_SIZE * 10];
unsigned char sbuffer[FRAME_SIZE * 10];

unsigned char recv_image[FRAME_SIZE];
unsigned char cap_image[FRAME_SIZE];
unsigned char cap_image_last[FRAME_SIZE];

// Used by callback (only has hwnd and data pointer, might reference by hwnd somehow later)
static int connect_sframe_rvoice_sock = -1;
static int capture = 1;



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
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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
	static RECT		client_area;
	static bool init = false;
	static int server_sock = -1;	// listen socket
	static int client_svoice_rframe_sock = -1;	// client socket from listen

	static client_state_t connect_state;
	static client_state_t client_state;
	static int listen_port = 65535;
	static int connect_port = 65534;
	static char connect_ip[MAX_PATH] = "127.0.0.1";
	static Audio audio;
	static Voice voice;
	static bool enable_voice = false;

	switch (message)
	{
	case WM_CREATE:
	{
		audio.init();
		voice.init(audio);
		audio.capture_start();
		WSAStartup(MAKEWORD(2, 0), &WSAData);
		RedirectIOToConsole(true);
		GetClientRect(hwnd, &client_area);

		SetTimer(hwnd, 0, 500, NULL);

		char path[MAX_PATH] = { 0 };
		GetCurrentDirectory(MAX_PATH, path);
		lstrcat(path, TEXT("\\zoomy.ini"));

		listen_port = GetPrivateProfileInt(TEXT("zoomy"), TEXT("listen"), 65535, path);
		connect_port = GetPrivateProfileInt(TEXT("zoomy"), TEXT("connect"), 65534, path);
		GetPrivateProfileString(TEXT("zoomy"), TEXT("ip"), "127.0.0.1", connect_ip, MAX_PATH, path);
		capture = GetPrivateProfileInt(TEXT("zoomy"), TEXT("capture"), 1, path);


		// Create a checkerboard pattern
		unsigned int *ipixel = (unsigned int *)blue_check;
		for (int i = 0; i < WIDTH; i++)
		{
			for (int j = 0; j < HEIGHT; j++)
			{
				unsigned char c = (((i & 0x8) == 0) ^ ((j & 0x8) == 0)) * 255;
				ipixel[i + j * WIDTH + 0] = c;
			}
		}


		ipixel = (unsigned int *)red_check;
		for (int i = 0; i < WIDTH; i++)
		{
			for (int j = 0; j < HEIGHT; j++)
			{
				unsigned char c = (((i & 0x8) == 0) ^ ((j & 0x8) == 0)) * 255;
				ipixel[i + j * WIDTH + 0] = RGB(0, 0, c); // RGB is backwards so red is blue etc
			}
		}

		ipixel = (unsigned int *)grey_check;
		for (int i = 0; i < WIDTH; i++)
		{
			for (int j = 0; j < HEIGHT; j++)
			{
				unsigned char c = (((i & 0x8) == 0) ^ ((j & 0x8) == 0)) * 255;
				ipixel[i + j * WIDTH + 0] = RGB(c / 4, c / 4, c / 4);
			}
		}

		memcpy(cap_image, grey_check, FRAME_SIZE);
		memcpy(recv_image, grey_check, FRAME_SIZE);

		if (capture)
		{
			printf("Starting Camera\r\n");
			camhwnd = capCreateCaptureWindow("camera window", WS_CHILD | WS_VISIBLE, WIDTH, HEIGHT, WIDTH, HEIGHT, hwnd, 0);
		}
		else
		{
			printf("*** Camera not enabled ***\r\n");
			memcpy(cap_image, blue_check, FRAME_SIZE);
		}


		listen_socket(server_sock, listen_port);
		set_sock_options(server_sock);

		break;
	}
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_SPACE:
			enable_voice = !enable_voice;
			break;
		}
		break;
	case WMU_CAPTURE:
	{
		int rsize = 0;

		if (init == false)
			return 0;

		if (connect_state == CONNECTED && enable_voice)
		{
			int ret = voice.voice_recv(audio, connect_sframe_rvoice_sock);
			if (ret == -1)
			{
				connect_state = DISCONNECTED;
			}
		}


		if (client_svoice_rframe_sock == SOCKET_ERROR)
			handle_listen(server_sock, client_svoice_rframe_sock);
		else
			handle_accepted(client_svoice_rframe_sock, (char *)rbuffer, rsize);

		while (rqueue.size >= FRAME_SIZE / 2)
		{
			dequeue(&rqueue, rbuffer, FRAME_SIZE / 2);

			yuy2_to_rgb(rbuffer, (COLORREF *)recv_image);
			InvalidateRect(hwnd, &client_area, FALSE);
			static int i = 0;
			printf("Showing frame %d\r\n", i++);
		}

		while (squeue.size >= FRAME_SIZE / 2 && connect_sframe_rvoice_sock != SOCKET_ERROR)
		{
			dequeue(&squeue, sbuffer, FRAME_SIZE / 2);
			int ret = send(connect_sframe_rvoice_sock, (char *)sbuffer, FRAME_SIZE / 2, 0);
			if (ret == -1)
			{
				int err = WSAGetLastError();

				if (err != WSAEWOULDBLOCK)
				{
					printf("send returned -1 error %d\r\n", err);
					connect_state = DISCONNECTED;
					closesocket(connect_sframe_rvoice_sock);
					connect_sframe_rvoice_sock = -1;
				}
				break;
			}
			else if (ret > 0 && ret < FRAME_SIZE / 2)
			{
				// partial send occurred (full buffer?)
				enqueue_front(&squeue, &sbuffer[ret], FRAME_SIZE / 2 - ret);
			}


		}


		if (enable_voice)
		{
			voice.voice_send(audio, client_svoice_rframe_sock);
		}

		// Old way of getting frame RGB from window
		// Using RAW YUY2 means half the size using callback
#if 0
		if (sock != SOCKET_ERROR && capture)
		{
			getBitmapFromWindow(camhwnd, 0, 0, WIDTH, HEIGHT, cap_image);
			// prevent duplicate frames
			if (memcmp(cap_image, cap_image_last, FRAME_SIZE) != 0)
			{
				enqueue(&squeue, cap_image, FRAME_SIZE);
				memcpy(cap_image_last, cap_image, FRAME_SIZE);
			}
			else
			{
				printf("Duplicate frame\r\n");
			}
		}
#endif

		if (connect_sframe_rvoice_sock != SOCKET_ERROR && capture == 0)
		{
			static int i = 0;
			// send checkerbox

			if (i % 500 == 0)
			{
				printf("Sending checkbox %d\r\n", i++);
				enqueue(&squeue, cap_image, FRAME_SIZE);
			}
		}

		break;
	}
	case WM_TIMER:
	{
		// Dont attempt anything until video is streaming
		if (init == false)
			return 0;

		if (connect_sframe_rvoice_sock == -1 || connect_state == DISCONNECTED)
		{
			int ret = connect_socket(connect_ip, connect_port, connect_sframe_rvoice_sock);
			if (ret == 0)
			{
				connect_state = CONNECTED;
			}
			else
			{
				//				memset(&rqueue, 0, sizeof(queue_t));
				connect_state = DISCONNECTED;
			}
		}

		if (client_svoice_rframe_sock == SOCKET_ERROR)
		{
			// add check box to rqueue to show on remote side, note redish hue now to YUYV format
			//enqueue(&rqueue, red_check, FRAME_SIZE);
			client_state = DISCONNECTED;
		}
		else
		{
			client_state = CONNECTED;
		}

		break;
	}
	case WM_SIZE:
	{
		int	width, height;

		width = LOWORD(lParam);
		height = HIWORD(lParam);

		if (capture)
		{
			// setup frame rate
			//			CAPTUREPARMS CaptureParms;
			//			float FramesPerSec = 30.0; // 30 frames per second

			//			capCaptureGetSetup(camhwnd, &CaptureParms, sizeof(CAPTUREPARMS));
			//			CaptureParms.dwRequestMicroSecPerFrame = (DWORD)(1.0e6 / FramesPerSec);
			//			capCaptureSetSetup(camhwnd, &CaptureParms, sizeof(CAPTUREPARMS));

			// setup resolution
			BITMAPINFO psVideoFormat;

			capGetVideoFormat(camhwnd, &psVideoFormat, sizeof(psVideoFormat));
			psVideoFormat.bmiHeader.biWidth = WIDTH;
			psVideoFormat.bmiHeader.biHeight = HEIGHT;
			capSetVideoFormat(camhwnd, &psVideoFormat, sizeof(psVideoFormat));


			CAPDRIVERCAPS CapDrvCaps;

			capDriverGetCaps(camhwnd, &CapDrvCaps, sizeof(CAPDRIVERCAPS));

			if (CapDrvCaps.fHasOverlay)
				capOverlay(camhwnd, TRUE); //for speedup

			SetWindowPos(camhwnd, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0);
			capSetCallbackOnFrame(camhwnd, frameCallback);
			SendMessage(camhwnd, WM_CAP_DRIVER_CONNECT, 0, 0);

			SendMessage(camhwnd, WM_CAP_DLG_VIDEOFORMAT, 0, 0);
			capDlgVideoCompression(camhwnd);

			SendMessage(camhwnd, WM_CAP_SET_SCALE, true, 0);
			SendMessage(camhwnd, WM_CAP_SET_PREVIEWRATE, 16, 0);
			SendMessage(camhwnd, WM_CAP_SET_PREVIEW, true, 0);
		}
		init = true;

		break;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC		hdc;

		hdc = BeginPaint(hwnd, &ps);

		draw_pixels(hdc, DISPLAY_WIDTH, 0, WIDTH, HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT, recv_image);

		if (capture == 0)
		{
			// capture will draw himself, so no need to do anything
			draw_pixels(hdc, 0, 0, WIDTH, HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT, cap_image);
		}

		char state[MAX_PATH];

		sprintf(state, "Connect socket %d, connected=%d. client socket %d, connected=%d voice enable=%d (space)",
			connect_sframe_rvoice_sock,
			connect_state == CONNECTED,
			client_svoice_rframe_sock,
			client_state == CONNECTED,
			enable_voice
		);
		TextOut(hdc, 50, 500, state, strlen(state));

		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_DESTROY:
	{
		if (capture)
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


int listen_socket(int &sock, unsigned short port)
{
	struct sockaddr_in	servaddr;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	memset(&servaddr, 0, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);

	bind(sock, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in));
	listen(sock, 3);
	printf("listening on port %d\n", port);
	return 0;
}

int set_sock_options(int sock)
{
	unsigned long nonblock = 1;
	ioctlsocket(sock, FIONBIO, &nonblock);
	unsigned int sndbuf;
	unsigned int rcvbuf;

	socklen_t arglen = sizeof(int);

	getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, &arglen);
	printf("SO_SNDBUF = %d\n", sndbuf);

	getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, &arglen);
	printf("SO_RCVBUF = %d\n", rcvbuf);

	sndbuf = 65507; //default 8192
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, sizeof(sndbuf));
	printf("Setting SO_SNDBUF to %d\n", sndbuf);
	getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, &arglen);
	printf("SO_SNDBUF = %d\n", sndbuf);

	rcvbuf = 65507; //default 8192
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, sizeof(rcvbuf));
	printf("Setting SO_RCVBUF to %d\n", rcvbuf);
	getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, &arglen);
	printf("SO_RCVBUF = %d\n", rcvbuf);



	int flag = 1;
	int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

	return 0;
}


int connect_socket(char *ip_addr, unsigned short port, int &sock)
{
	struct sockaddr_in	servaddr;
	int ret;

	if (sock == -1)
	{
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		set_sock_options(sock);

		memset(&servaddr, 0, sizeof(struct sockaddr_in));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = inet_addr(ip_addr);
		servaddr.sin_port = htons(port);

		// 3 way handshake
		printf("Attempting to connect to %s:%d\n", ip_addr, port);
		ret = connect(sock, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in));

		if (ret == SOCKET_ERROR)
		{
			ret = WSAGetLastError();

			switch (ret)
			{
			case WSAETIMEDOUT:
				printf("Fatal Error: Connecting to %s timed out.\n", ip_addr);
				break;
			case WSAECONNREFUSED:
				printf("Fatal Error: %s refused connection.\n(Server program is not running)\n", ip_addr);
				break;
			case WSAEHOSTUNREACH:
				printf("Fatal Error: router sent ICMP packet (destination unreachable)\n");
				break;
			case WSAEWOULDBLOCK:
				printf("Would block, using select()\r\n");
				return -1;
			default:
				printf("Fatal Error: %d\n", ret);
				break;
			}

			return -1;
		}

		printf("TCP handshake completed with %s\n", ip_addr);
		return 0;
	}
	else
	{

		struct timeval timeout;
		fd_set read_set;
		fd_set write_set;

		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		FD_SET(sock, &read_set);
		FD_SET(sock, &write_set);

		timeout.tv_sec = 0;
		timeout.tv_usec = 1;

		int ret = select(sock + 1, &read_set, &write_set, NULL, &timeout);
		if (ret < 0)
		{
			printf("select() failed ");
			return -1;
		}
		else if (ret == 0)
		{
			static int count = 0;
			printf("select() timed out\r\n");
			count++;

			if (count == 5 * (200000))
			{
				closesocket(sock);
				sock = -1;
			}
			return -1;
		}

		if (FD_ISSET(sock, &read_set) || FD_ISSET(sock, &write_set))
		{
			unsigned int err = -1;
			int len = 4;
			int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
			if (ret != 0)
			{
				printf("getsockopt failed\r\n");
			}

			if (err == 0)
			{
				printf("TCP handshake completed with %s\n", ip_addr);
				return 0;
			}
			else
			{
				closesocket(sock);
				sock = -1;
			}

		}
	}

	return -1;
}

void handle_listen(int &sock, int &csock)
{
	struct sockaddr_in csockaddr;
	int addrlen = sizeof(sockaddr);

	csock = accept(sock, (sockaddr *)&csockaddr, &addrlen);
	if (csock != -1)
	{
		char ipstr[MAX_PATH] = { 0 };

		inet_ntop(AF_INET, &(csockaddr.sin_addr), ipstr, MAX_PATH);
		printf("Accepted connection from %s\n", ipstr);
		set_sock_options(csock);
	}
}


void handle_accepted(int &csock, char *buffer, int &size)
{
	if (csock == -1)
		return;

	while (1)
	{
		size = recv(csock, buffer, FRAME_SIZE / 2 + sizeof(int), 0);
		if (size > 0)
		{
			// add data to circular queue
			enqueue(&rqueue, rbuffer, size);
		}
		else if (size == 0)
		{
			break;
		}
		else if (size < 0)
		{
			int ret = WSAGetLastError();

			if (ret == WSAEWOULDBLOCK)
			{
				size = 0;
				return;
			}

			switch (ret)
			{
			case WSAETIMEDOUT:
				break;
			case WSAECONNREFUSED:
				break;
			case WSAEHOSTUNREACH:
				break;
			default:
				printf("Fatal Error: %d\n", ret);
				break;
			}

			csock = -1;
			csock = SOCKET_ERROR;
			break;
		}
	}
}

void draw_pixels(HDC hdc, int xoff, int yoff, int width, int height, int scalew, int scaleh, unsigned char *data)
{
	HBITMAP hBitmap, hOldBitmap;
	HDC hdcMem;

	hBitmap = CreateCompatibleBitmap(hdc, width, height);
	SetBitmapBits(hBitmap, sizeof(int) * width * height, data);
	hdcMem = CreateCompatibleDC(hdc);
	hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

	// This scaling is a little strange because Stretch maintains aspect ratios
	StretchBlt(hdc, xoff, yoff, scalew, scaleh, hdcMem, 0, 0, width, height, SRCCOPY);
	SelectObject(hdcMem, hOldBitmap);
	DeleteDC(hdcMem);
	DeleteObject(hBitmap);
}



#define RGB2(b,g,r)          ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))

void yuy2_to_rgb(unsigned char *yuvData, COLORREF *data)
{
	int j = 0;

	for (int i = 0; i < WIDTH * HEIGHT; i += 2)
	{
		//YUY2 to ABGR
		BYTE Y0 = yuvData[j++];
		BYTE U = yuvData[j++];
		BYTE Y1 = yuvData[j++];
		BYTE V = yuvData[j++];

		data[i] = RGB2(
			(unsigned char)((float)Y0 + (1.4075f*(float)(V - 128))),
			(unsigned char)((float)Y0 + (0.3455f*(float)(U - 128) - (0.7169f*(float)(V - 128)))),
			(unsigned char)((float)Y0 + (1.7790f*(float)(U - 128)))
		);

		data[i + 1] = RGB2(
			(unsigned char)((float)Y1 + (1.4075f*(float)(V - 128))),
			(unsigned char)((float)Y1 + (0.3455f*(float)(U - 128) - (0.7169f*(float)(V - 128)))),
			(unsigned char)((float)Y1 + (1.7790f*(float)(U - 128)))
		);
	}
}


// Note: lpVHdr has YUY2 data not RGB
LRESULT frameCallback(HWND hWnd, LPVIDEOHDR lpVHdr)
{
	if (connect_sframe_rvoice_sock != SOCKET_ERROR && capture)
	{
		// prevent duplicate frames
		if (memcmp(cap_image, cap_image_last, FRAME_SIZE) != 0)
		{
			static int i = 0;
			printf("Sending Frame %d\r\n", i++);
			enqueue(&squeue, lpVHdr->lpData, lpVHdr->dwBytesUsed);
			memcpy(cap_image_last, lpVHdr->lpData, lpVHdr->dwBytesUsed);
		}
		else
		{
			printf("Duplicate frame\r\n");
		}
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
