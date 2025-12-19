#ifndef CHAT_CONFIG_H
#define CHAT_CONFIG_H


#include <sys/types.h>


#define SERVER_FIFO_PATH "/tmp/server_fifo"
#define CLIENT_FIFO_PATH_FORMAT "/tmp/client_fifo_%d"


#define MAX_FIFO_PATH 128
#define MAX_CONTENT 256
#define MAX_CLIENTS 128


// Message types
#define MSG_TYPE_CONNECT 1
#define MSG_TYPE_CHAT 2
#define MSG_TYPE_DISCONNECT 3


typedef struct {
	int type; // MSG_TYPE_*
	pid_t pid; // sender pid
	char fifo_path[MAX_FIFO_PATH]; // used for CONNECT
	char content[MAX_CONTENT]; // used for CHAT
} Message;

// Ensure message size fits in PIPE_BUF to keep writes atomic on FIFOs.
// Most POSIX systems guarantee PIPE_BUF >= 512. Our struct must be <= 512 bytes.
_Static_assert(sizeof(Message) <= 512, "Message exceeds PIPE_BUF; reduce fields or use a union");


#endif // CHAT_CONFIG_H
