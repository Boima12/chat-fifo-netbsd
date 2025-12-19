#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include "../include/chat_config.h"

static char client_fifo_path[MAX_FIFO_PATH];
static int server_fd = -1;
static int running = 1;

void sigint_handler(int signo) {
	(void)signo;
	running = 0;
}

void cleanup_client() {
	// send DISCONNECT
	if (server_fd != -1) {
		Message msg;
		memset(&msg, 0, sizeof(msg));
		msg.type = MSG_TYPE_DISCONNECT;
		msg.pid = getpid();
		strncpy(msg.fifo_path, client_fifo_path, MAX_FIFO_PATH-1);
		write(server_fd, &msg, sizeof(msg));
		close(server_fd);
	}

	unlink(client_fifo_path);
}

int main(void) {
	pid_t pid = getpid();
	snprintf(client_fifo_path, MAX_FIFO_PATH, CLIENT_FIFO_PATH_FORMAT, pid);

	// create private fifo
	if (mkfifo(client_fifo_path, 0666) == -1) {
		if (errno != EEXIST) {
			perror("mkfifo client");
			return 1;
		}
	}

	// open server FIFO for writing (blocks until server opens for reading)
	server_fd = open(SERVER_FIFO_PATH, O_WRONLY);
	if (server_fd == -1) {
		perror("open server fifo for write");
		unlink(client_fifo_path);
		return 1;
	}

	// send CONNECT message
	Message conn_msg;
	memset(&conn_msg, 0, sizeof(conn_msg));
	conn_msg.type = MSG_TYPE_CONNECT;
	conn_msg.pid = pid;
	strncpy(conn_msg.fifo_path, client_fifo_path, MAX_FIFO_PATH-1);
	if (write(server_fd, &conn_msg, sizeof(conn_msg)) != sizeof(conn_msg)) {
		perror("write connect");
		cleanup_client();
		return 1;
	}

	// setup SIGINT
	struct sigaction sa;
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);

	// fork: child for reading messages from private fifo
	pid_t child = fork();
	if (child == -1) {
		perror("fork");
		cleanup_client();
		return 1;
	}

	if (child == 0) {
		// Child: read from private fifo
		// Open in O_RDWR to ensure read doesn't return 0 when no external writer
		int priv_fd = open(client_fifo_path, O_RDWR);
		if (priv_fd == -1) {
			perror("open private fifo for read/write");
			exit(1);
		}

		while (1) {
			Message in;
			ssize_t r = read(priv_fd, &in, sizeof(in));
			if (r == -1) {
				if (errno == EINTR) continue;
				perror("read private fifo");
				break;
			} else if (r == 0) {
				// writer closed
				// sleep briefly and continue
				usleep(100000);
				continue;
			} else if (r != sizeof(in)) {
				continue;
			}

			if (in.type == MSG_TYPE_CHAT) {
				printf("[chat %d] %s\n", in.pid, in.content);
			}
		}
		close(priv_fd);
		exit(0);
	} else {
		// Parent: read stdin and send chat messages
		char line[MAX_CONTENT];
		while (running) {
			if (fgets(line, sizeof(line), stdin) == NULL) {
				if (feof(stdin)) break;
				if (errno == EINTR) continue;
				break;
			}

			// remove newline
			size_t len = strlen(line);
			if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

			// prepare message
			Message chat;
			memset(&chat, 0, sizeof(chat));
			chat.type = MSG_TYPE_CHAT;
			chat.pid = getpid();
			strncpy(chat.fifo_path, client_fifo_path, MAX_FIFO_PATH-1);
			strncpy(chat.content, line, MAX_CONTENT-1);

			if (write(server_fd, &chat, sizeof(chat)) != sizeof(chat)) {
				perror("write chat");
				break;
			}
		}

		// cleanup
		cleanup_client();
		// optionally kill child
		kill(child, SIGTERM);
		wait(NULL);
	}

	return 0;
}
