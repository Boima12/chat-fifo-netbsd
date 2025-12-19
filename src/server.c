#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include "../include/chat_config.h"

// Simple dynamic client list
typedef struct ClientNode {
	pid_t pid;
	char fifo_path[MAX_FIFO_PATH];
	struct ClientNode *next;
} ClientNode;

static int server_fd = -1;
static volatile sig_atomic_t keep_running = 1;
static ClientNode *clients_head = NULL;

void cleanup_and_exit(int status);

void sigint_handler(int signo) {
	(void)signo;
	keep_running = 0;
}

void add_client(pid_t pid, const char *fifo_path) {
	// check if exists
	ClientNode *cur = clients_head;
	while (cur) {
		if (cur->pid == pid) return; // already registered
		cur = cur->next;
	}
	ClientNode *node = malloc(sizeof(ClientNode));
	if (!node) return;
	node->pid = pid;
	strncpy(node->fifo_path, fifo_path, MAX_FIFO_PATH-1);
	node->fifo_path[MAX_FIFO_PATH-1] = '\0';
	node->next = clients_head;
	clients_head = node;
	printf("[server] Client %d registered (fifo=%s)\n", pid, node->fifo_path);
}

void remove_client(pid_t pid) {
	ClientNode **ptr = &clients_head;

	while (*ptr) {
		if ((*ptr)->pid == pid) {
			ClientNode *del = *ptr;
			*ptr = del->next;
			printf("[server] Client %d unregistered\n", del->pid);
			free(del);
			return;
		}
	ptr = &(*ptr)->next;
	}
}

void broadcast_message(const Message *msg) {
	ClientNode *cur = clients_head;

	while (cur) {
		if (cur->pid == msg->pid) {
			cur = cur->next; // skip sender
			continue;
		}

		// open client's FIFO for writing non-blocking
		int fd = open(cur->fifo_path, O_WRONLY | O_NONBLOCK);
		if (fd == -1) {
			// can't open – maybe client closed; skip
			// printf("[server] cannot open %s: %s\n", cur->fifo_path, strerror(errno));
			cur = cur->next;
			continue;
		}

		// write Message struct to client
		ssize_t w = write(fd, msg, sizeof(Message));
		if (w != sizeof(Message)) {
			// ignore partial
		}
		close(fd);
		cur = cur->next;
	}
}

void cleanup_clients() {
	ClientNode *cur = clients_head;

	while (cur) {
		ClientNode *n = cur->next;
		free(cur);
		cur = n;
	}
	clients_head = NULL;
}

void cleanup_and_exit(int status) {
	if (server_fd != -1) close(server_fd);
	unlink(SERVER_FIFO_PATH);
	cleanup_clients();
	printf("[server] cleaned up and exiting\n");
	exit(status);
}

int main(void) {
	// set up SIGINT handler
	struct sigaction sa;
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);

	// create server FIFO if not exists
	if (mkfifo(SERVER_FIFO_PATH, 0666) == -1) {
		if (errno != EEXIST) {
			perror("mkfifo");
			return 1;
		}
	}

	printf("[server] Starting. Server FIFO: %s\n", SERVER_FIFO_PATH);

	// open server FIFO for reading + writing (to avoid blocking on open)
	server_fd = open(SERVER_FIFO_PATH, O_RDWR);
	if (server_fd == -1) {
		perror("open server fifo");
		cleanup_and_exit(1);
	}

	while (keep_running) {
		Message msg;
		ssize_t r = read(server_fd, &msg, sizeof(Message));
		if (r == -1) {
			if (errno == EINTR) continue;
			perror("read server fifo");
			break;
		} else if (r == 0) {
			// all writers closed; reopen
			close(server_fd);
			server_fd = open(SERVER_FIFO_PATH, O_RDWR);
			if (server_fd == -1) {
				perror("reopen server fifo");
				break;
			}
			continue;
		} else if (r != sizeof(Message)) {
			// partial read -- ignore
			continue;
		}

		if (msg.type == MSG_TYPE_CONNECT) {
			add_client(msg.pid, msg.fifo_path);

			printf("[server] Client connected: pid=%d fifo=%s\n", msg.pid, msg.fifo_path);

		} else if (msg.type == MSG_TYPE_DISCONNECT) {
			remove_client(msg.pid);
		} else if (msg.type == MSG_TYPE_CHAT) {
			printf("[server] chat from %d: %s\n", msg.pid, msg.content);
			printf("[server] broadcasting to %d other clients\n", 0); // counting clients logic here
			// broadcast
			broadcast_message(&msg);
		} else {
			// unknown
		}
	}


	cleanup_and_exit(0);
	return 0;
}
