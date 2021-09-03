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

#define WMU_CAPTURE WM_USER + 1


typedef int socklen_t;

unsigned int cxClient, cyClient;

char *inet_ntop4(const u_char *src, char *dst, socklen_t size)
{
	static const char fmt[] = "%u.%u.%u.%u";
	char tmp[sizeof "255.255.255.255"];
	int l;

	l = snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]);
	if (l <= 0 || (socklen_t)l >= size) {
		return (NULL);
	}
	strncpy(dst, tmp, size);
	return (dst);
}

char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	switch (af) {
	case AF_INET:
		return (inet_ntop4((u_char *)src, (char *)dst, size));
	default:
		return (NULL);
	}
	/* NOTREACHED */
}


LRESULT CALLBACK WinProc(HWND, UINT, WPARAM, LPARAM);


#pragma pack(push, 1)
typedef struct
{
	int size;
	int width;
	int height;
	short planes;
	short bpp;
	int compression;
	int image_size;
	int xres;
	int yres;
	int clr_used;
	int clr_important;
} dib_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	char type[2];
	int file_size;
	int reserved;
	int offset;
} bmpheader_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	bmpheader_t	header;
	dib_t		dib;
} bitmap_t;
#pragma pack(pop)


unsigned char data[640 * 480 * 4];
unsigned char rbuffer[640 * 480 * 4];
unsigned char pixel[640 * 480 * 4];

#define SIZE_QUEUE 640*480*4*10 // 10 frames

typedef struct
{
	int size;
	int tail;
	int head;
	char buffer[SIZE_QUEUE];
} queue_t;



queue_t rqueue;


int enqueue(queue_t *queue, unsigned char *buffer, int size)
{
	int        i;

	if (queue->size == SIZE_QUEUE)
	{
		return 0;
	}

	for (i = 0; i < size && queue->size != SIZE_QUEUE; i++)
	{
		queue->buffer[queue->tail++] = buffer[i];
		queue->size++;
		if (queue->tail == SIZE_QUEUE)
			queue->tail = 0;
	}
	return i;
}

int enqueue_front(queue_t *queue, unsigned char *buffer, int size)
{
	int        i;

	if (queue->size == SIZE_QUEUE)
		return 0;

	for (i = 0; i < size && queue->size != SIZE_QUEUE; i++)
	{
		if (queue->head == 0)
		{
			queue->head = SIZE_QUEUE;
		}
		queue->buffer[--queue->head] = buffer[(size - 1) - i];
		queue->size++;
	}
	return i;
}


int dequeue(queue_t *queue, unsigned char *buffer, int size)
{
	int        i;

	for (i = 0; i < size && queue->size != 0; i++)
	{
		buffer[i] = queue->buffer[queue->head++];
		//queue->buffer[queue->head - 1] = ' '; // empty space for debugging
		queue->size--;
		if (queue->head == SIZE_QUEUE)
		{
			queue->head = 0;
		}
	}
	return i;
}

int dequeue_peek(queue_t *queue, unsigned char *buffer, int size)
{
	int        i;
	int		head = queue->head;
	int		qsize = queue->size;

	for (i = 0; i < size && qsize != 0; i++)
	{
		buffer[i] = queue->buffer[head++];
		//queue->buffer[queue->head - 1] = ' '; // empty space for debugging
		qsize--;
		if (head == SIZE_QUEUE)
		{
			head = 0;
		}
	}
	return i;
}


void RedirectIOToConsole(int debug)
{
	if (debug)
	{
		int	hConHandle;
		long	lStdHandle;
		FILE	*fp;
		CONSOLE_SCREEN_BUFFER_INFO	coninfo;

		AllocConsole();
		HWND hwndConsole = GetConsoleWindow();

		ShowWindow(hwndConsole, SW_SHOW);
		// set the screen buffer to be big enough to let us scroll text
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);

		coninfo.dwSize.Y = 512;
		SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

		// redirect unbuffered STDOUT to the console
		lStdHandle = (intptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
		hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);

		fp = _fdopen(hConHandle, "w");
		*stdout = *fp;
		setvbuf(stdout, NULL, _IONBF, 0);

		// redirect unbuffered STDIN to the console
		lStdHandle = (intptr_t)GetStdHandle(STD_INPUT_HANDLE);
		hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);

		fp = _fdopen(hConHandle, "r");
		*stdin = *fp;
		setvbuf(stdin, NULL, _IONBF, 0);

		// redirect unbuffered STDERR to the console
		lStdHandle = (intptr_t)GetStdHandle(STD_ERROR_HANDLE);
		hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);
		fp = _fdopen(hConHandle, "w");
		*stderr = *fp;
		setvbuf(stderr, NULL, _IONBF, 0);

		// make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog point to console as well
		//ios::sync_with_stdio();

		//Fix issue on windows 10
		FILE *fp2 = freopen("CONOUT$", "w", stdout);
	}
	else
	{
		freopen("altEngine.log", "a", stdout);
		freopen("altEngine.log", "a", stderr);
	}
}


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


void write_bitmap(char *filename, int width, int height, int *data)
{
	FILE *file;
	bitmap_t	bitmap;

	memset(&bitmap, 0, sizeof(bitmap_t));
	memcpy(bitmap.header.type, "BM", 2);
	bitmap.header.offset = sizeof(bmpheader_t);
	bitmap.dib.size = sizeof(dib_t);
	bitmap.dib.width = width;
	bitmap.dib.height = height;
	bitmap.dib.planes = 1;
	bitmap.dib.bpp = 32;
	bitmap.dib.compression = 0;
	bitmap.dib.image_size = width * height * sizeof(int);
	bitmap.header.file_size = sizeof(bmpheader_t) + sizeof(dib_t) + bitmap.dib.image_size;

	file = fopen(filename, "wb");
	if (file == NULL)
	{
		perror("Unable to write file");
		return;
	}

	fwrite(&bitmap, 1, sizeof(bitmap_t), file);
	fwrite((void *)data, 1, width * height * 4, file);
	fclose(file);
}



int listen_sock(SOCKET &sock, unsigned short port)
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

int set_sock_options(SOCKET sock)
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

	return 0;
}


int connect_sock(char *ip_addr, unsigned short port, SOCKET &sock)
{
	struct sockaddr_in	servaddr;
	int ret;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);


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
		default:
			printf("Fatal Error: %d\n", ret);
			break;
		}

		return -1;
	}
	printf("TCP handshake completed with %s\n", ip_addr);
	set_sock_options(sock);

	return 0;
}






void handle_listen(SOCKET &sock, SOCKET &csock)
{
	struct sockaddr_in csockaddr;
	int addrlen = sizeof(sockaddr);

	csock = accept(sock, (sockaddr *)&csockaddr, &addrlen);
	if (csock != -1)
	{
		char ipstr[80] = { 0 };
		inet_ntop(AF_INET, &(csockaddr.sin_addr), ipstr, 80);
		printf("Accepted connection from %s\n", ipstr);
		set_sock_options(csock);
	}
}


void handle_accepted(SOCKET &csock, char *buffer, int &size)
{
	if (csock == -1)
		return;

	size = recv(csock, buffer, 640 * 480 * 4, 0);
	if (size == -1)
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
	}

}

void draw_pixels(HDC hdc, HDC hdcMem, int width, int height)
{
	HBITMAP hBitmap, hOldBitmap;

	hBitmap = CreateCompatibleBitmap(hdc, width, height);
	SetBitmapBits(hBitmap, sizeof(int) * width * height, pixel);
	hdcMem = CreateCompatibleDC(hdc);
	hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

	// This scaling is a little strange because Stretch maintains aspect ratios
	StretchBlt(hdc, 640, 0, width, height, hdcMem, 0, 0, width, height, SRCCOPY);
	SelectObject(hdcMem, hOldBitmap);
	DeleteDC(hdcMem);
	DeleteObject(hBitmap);
}

void draw_pixels2(HDC hdc, HDC hdcMem, int width, int height)
{
	HBITMAP hBitmap, hOldBitmap;

	hBitmap = CreateCompatibleBitmap(hdc, width, height);
	SetBitmapBits(hBitmap, sizeof(int) * width * height, data);
	hdcMem = CreateCompatibleDC(hdc);
	hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

	// This scaling is a little strange because Stretch maintains aspect ratios
	StretchBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, width, height, SRCCOPY);
	SelectObject(hdcMem, hOldBitmap);
	DeleteDC(hdcMem);
	DeleteObject(hBitmap);
}

LRESULT CALLBACK WinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HINSTANCE hInstance = GetModuleHandle(NULL);

	static HWND camhwnd = NULL;
	static HDC hdc;
	static HDC hdcMem;
	static PAINTSTRUCT ps;
	static HBITMAP hbm;
	static RECT rc;
	static RECT rect;
	static bool init = false;
	static WSADATA	WSAData;
	static SOCKET sock = -1;
	static SOCKET lsock = -1;
	static SOCKET csock = -1;

	static int listen_port = 65535;
	static int connect_port = 65534;
	static char connect_ip[80] = "127.0.0.1";
	static int capture = 1;
	static Audio audio;
	static Voice voice;

	switch (message)                  /* handle the messages */
	{
	case WM_CREATE:
	{
		audio.init();
		voice.init(audio);
		audio.capture_start();
		WSAStartup(MAKEWORD(2, 0), &WSAData);
		RedirectIOToConsole(true);
		GetClientRect(hwnd, &rect);

		cxClient = rect.right;
		cyClient = rect.bottom;

		SetTimer(hwnd, 0, 5000, NULL);

		char path[MAX_PATH] = { 0 };
		GetCurrentDirectory(MAX_PATH, path);
		lstrcat(path, TEXT("\\zoomy.ini"));

		listen_port = GetPrivateProfileInt(TEXT("zoomy"), TEXT("listen"), 65535, path);
		connect_port = GetPrivateProfileInt(TEXT("zoomy"), TEXT("connect"), 65534, path);
		GetPrivateProfileString(TEXT("zoomy"), TEXT("ip"), "127.0.0.1", connect_ip, 80, path);
		capture = GetPrivateProfileInt(TEXT("zoomy"), TEXT("capture"), 1, path);


		if (capture)
		{
			printf("Starting Camera\r\n");
			camhwnd = capCreateCaptureWindow("camera window", WS_CHILD | WS_VISIBLE, 640, 480, 640, 480, hwnd, 0);
		}
		else
		{
			printf("*** Camera not enabled ***\r\n");


			// Create a checkerboard pattern
			unsigned int *ipixel = (unsigned int *)data;
			for (int i = 0; i < 640; i++)
			{
				for (int j = 0; j < 480; j++)
				{
					unsigned char c = (((i & 0x8) == 0) ^ ((j & 0x8) == 0)) * 255;
					ipixel[i + j * 640 + 0] = c;
				}
			}
		}


		listen_sock(lsock, listen_port);
		set_sock_options(lsock);

		break;
	}
	case WMU_CAPTURE:
	{
		int rsize = 0;

		if (init == false)
			return 0;

		if (csock != -1)
		{
			voice.voice_send(audio, csock);
		}

		if (sock != -1 && capture == 1)
		{
			voice.voice_recv(audio, sock);
		}


		if (csock == -1)
			handle_listen(lsock, csock);
		else
			handle_accepted(csock, (char *)rbuffer, rsize);

		if (rsize == -1)
			csock = -1;

		if (rsize > 0)
		{
			// add data to circular queue
			enqueue(&rqueue, rbuffer, rsize);
		}

		if (sock != -1 && capture)
		{
			HDC hDC = GetDC(camhwnd);
			HDC hTargetDC = CreateCompatibleDC(hDC);
			RECT rect = { 0, 0, 640, 480 };

			HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rect.right - rect.left,
				rect.bottom - rect.top);
			SelectObject(hTargetDC, hBitmap);
			PrintWindow(camhwnd, hTargetDC, PW_CLIENTONLY);

			GetBitmapBits(hBitmap, 640 * 480 * 4, &data);

			static int i = 0;
			printf("Sending Frame %d\r\n", i++);
			int ret = send(sock, (char *)data, 640 * 480 * 4, 0);

			if (ret == -1)
			{
				int err = WSAGetLastError();

				if (err != WSAEWOULDBLOCK)
				{
					printf("connect_sock return -1 error %d\r\n", err);
					sock = -1;
				}
			}

			DeleteObject(hBitmap);
			ReleaseDC(camhwnd, hDC);
			DeleteDC(hTargetDC);
		}

		if (rqueue.size >= 640 * 480 * 4)
		{
			//			char filename[80];
			dequeue(&rqueue, rbuffer, 640 * 480 * 4);

			memcpy(pixel, rbuffer, 640 * 480 * 4);
			InvalidateRect(hwnd, &rect, FALSE);
			static int i = 0;
			//			sprintf(filename, "frame%04d.bmp", i++);
			//			write_bitmap(filename, 640, 480, (int *)rbuffer);
			printf("Writing frame %d\r\n", i++);
		}

		if (sock != -1 && capture == 0)
		{
			int ret = send(sock, (char *)data, 640 * 480 * 4, 0);
		}

		break;
	}
	case WM_TIMER:
	{
		// Dont attempt anything until video is streaming
		if (init == false)
			return 0;


		if (sock == -1)
		{
			connect_sock(connect_ip, connect_port, sock);
		}


		break;
	}
	case WM_SIZE:
	{
		int	width, height;

		width = LOWORD(lParam);
		height = HIWORD(lParam);

		cxClient = width;
		cyClient = height;


		if (capture)
		{
			SetWindowPos(camhwnd, 0, 0, 0, width, height, 0);
			SendMessage(camhwnd, WM_CAP_DRIVER_CONNECT, 0, 0);
			SendMessage(camhwnd, WM_CAP_DLG_VIDEOFORMAT, 0, 0);

			SendMessage(camhwnd, WM_CAP_SET_SCALE, false, 0);
			SendMessage(camhwnd, WM_CAP_SET_PREVIEWRATE, 16, 0);
			SendMessage(camhwnd, WM_CAP_SET_PREVIEW, true, 0);
		}
		init = true;

		break;
	}

	case WM_PAINT:
		hdc = BeginPaint(hwnd, &ps);

		draw_pixels(hdc, hdcMem, 640, 480);

		if (capture == 0)
		{
			draw_pixels2(hdc, hdcMem, 640, 480);
		}

		EndPaint(hwnd, &ps);
		return 0;

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
