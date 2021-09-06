#ifndef ZOOMY_H
#define ZOOMY_H

#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <vfw.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "queue.h"
#include "sock.h"

#include "audio.h"
#include "voice.h"


#define WIDTH 320
#define HEIGHT 240
#define FRAME_SIZE (WIDTH * HEIGHT * 4)

#define DISPLAY_WIDTH 640
#define DISPLAY_HEIGHT 480


class Zoomy
{
public:
	Zoomy();
	void init(void *param1, void *param2);
	void capture();
	void resize(int width, int height);
	void step();
	void keydown(int key);
	void paint(HDC hdc);
	void destroy();

	// Normally these classes attempt to be operating system agnostic... but not today
	static DWORD WINAPI VoiceThread(LPVOID lpParam);
	static LRESULT frameCallback(HWND hWnd, LPVIDEOHDR lpVHdr);


	// Used by callback (only has hwnd and data pointer, might reference by hwnd somehow later)
	static int connect_sframe_sock;
	static int enable_video;
	static bool enable_voice;


private:

	void draw_pixels(HDC hdc, int xoff, int yoff, int width, int height, int scalew, int scaleh, unsigned char *data);
	int listen_socket(int &sock, unsigned short port);
	void yuy2_to_rgb(unsigned char *yuvData, COLORREF *data);
	void read_socket(int &csock, char *buffer, int &size);
	int connect_socket(char *ip_addr, unsigned short port, int &sock);
	int set_sock_options(int sock);
	void handle_listen(int &sock, int &csock, char *ipstr);



	bool initialized;
	int server_sock;	// listen socket
	int client_rframe_sock;	// client socket from listen

	client_state_t connect_state;
	client_state_t client_state;
	int listen_port;
	int connect_port;
	char connect_ip[512];
	char listen_ip[512];
	char client_ip[512];

	// again usually class is operating system agnostic
	HWND hwnd;
	HWND camhwnd;
	RECT client_area;

};

#endif