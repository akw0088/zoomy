#ifndef QUEUE_H
#define QUEUE_H
#define FRAME_SIZE (640 * 480 * 4)
#define SIZE_QUEUE (FRAME_SIZE * 10) // 10 frames

typedef struct
{
	int size;
	int tail;
	int head;
	char buffer[SIZE_QUEUE];
} queue_t;

int enqueue(queue_t *queue, unsigned char *buffer, int size);
int dequeue(queue_t *queue, unsigned char *buffer, int size);
int enqueue_front(queue_t *queue, unsigned char *buffer, int size);
#endif