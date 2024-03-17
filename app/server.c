#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be visible
	// when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage

	int server_fd;
	socklen_t client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting REUSE_PORT
	// ensures that we don't run into 'Address already in use' errors
	const int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) <
		0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) !=
		0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	const int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);

	const int client_fd =
		accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	printf("Client connected\n");

	if (client_fd < 0) {
		printf("Accept failed: %s \n", strerror(errno));
		return 1;
	}

	char input_buffer[1024];
	if (read(client_fd, input_buffer, sizeof(input_buffer)) < 0) {
		printf("Read failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Received request: %s\n", input_buffer);

	strtok(input_buffer, " ");
	char *path = strtok(NULL, " ");

	const char *ok = "HTTP/1.1 200 OK\r\n";
	if (strcmp(path, "/") == 0) {
		send(client_fd, ok, strlen(ok), 0);
	} else {
		path = strtok(path, "/");
		if (strcmp(path, "echo") == 0) {
			const char *content_type = "Content-Type: text/plain\r\n";
			const char *content_length = "Content-Length: ";
			const char *body = strtok(NULL, "/");
			body = NULL == body ? "" : body;

			char body_length[32];
			sprintf(body_length, "%ld", strlen(body));

			char response[1024];
			int full_length =
				sprintf(response, "%s%s%s%s\r\n\r\n%s", ok, content_type,
						content_length, body_length, body);

			send(client_fd, response, full_length, 0);
		} else {
			const char *not_found = "HTTP/1.1 404 Not Found\r\n";
			send(client_fd, not_found, strlen(not_found), 0);
		}
	}

	send(client_fd, "\r\n\r\n", 4, 0);
	close(server_fd);

	return 0;
}
