
#include <vector>
#include "audio.h"
#include <opus.h>

#ifndef VOICE_H
#define VOICE_H
#define VOICECHAT





typedef struct
{
	unsigned short int sequence;
	unsigned short int qport;
	unsigned char data[8192];
} voicemsg_t;

typedef struct
{
	char			socketname[32];
	unsigned int	qport;
	int				ent_id;
	unsigned short int	client_sequence;
	unsigned short int	server_sequence;
	unsigned int	last_time;
//	input_t			input;
//	netinfo_t		netinfo;
	bool			needs_state;
//	vec3			position_delta;
} client_t;


class Voice
{
public:
	Voice();
	int init(Audio &audio);
	void destroy();
	int encode(unsigned short *pcm, unsigned int size, unsigned char *data, int &num_bytes);
	int decode(unsigned char *data, unsigned short *pcm, unsigned int &size);
	int voice_send(Audio &audio, int sock);
	int voice_recv(Audio &audio, int sock);

	char server[128];
private:
#ifdef VOICECHAT
	OpusEncoder *encoder;
	OpusDecoder *decoder;
#endif
	
//	Socket		sock;
	unsigned short qport;
	unsigned short int		voice_send_sequence;
	unsigned short int		voice_recv_sequence;


#define NUM_PONG 2
	unsigned int mic_buffer[NUM_PONG];
	unsigned int mic_source;
	unsigned short mic_pcm[NUM_PONG][SEGMENT_SIZE];

	unsigned int decode_buffer[NUM_PONG];
	unsigned short decode_pcm[NUM_PONG][SEGMENT_SIZE];
	unsigned int decode_source;

};

#endif
