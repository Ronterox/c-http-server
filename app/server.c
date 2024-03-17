#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void handle_client(int client_fd) {
	if (client_fd < 0) {
		printf("Accept failed: %s \n", strerror(errno));
		return;
	}

	char input_buffer[1024];
	if (read(client_fd, input_buffer, sizeof(input_buffer)) < 0) {
		printf("Read failed: %s \n", strerror(errno));
		return;
	}

	printf("Received request: %s\n", input_buffer);
	strtok(input_buffer, " ");
	char *path = strtok(NULL, " ");

	const char ok[] = "HTTP/1.1 200 OK\r\n";
	if (strcmp(path, "/") == 0) {
		const char *response = "HTTP/1.1 200 OK\r\n"
							   "Content-Length: 0";
		send(client_fd, response, strlen(response), 0);
	} else if (strncmp(path, "/echo/", 6) == 0) {
		const char *body = strlen(path) > 6 ? path + 6 : "";

		char response[1024];
		int full_length = sprintf(response,
								  "%sContent-Type: text/plain\r\n"
								  "Content-Length: %ld\r\n\r\n%s",
								  ok, strlen(body), body);

		send(client_fd, response, full_length, 0);
	} else if (strcmp(path, "/user-agent") == 0) {
		char *line;
		do {
			line = strtok(NULL, "\r\n");
			if (strncmp(line, "User-Agent: ", 12) == 0) {
				char *user_agent = line + 12;
				char response[1024];
				int full_length = sprintf(response,
										  "%sContent-Type: text/plain\r\n"
										  "Content-Length: %ld\r\n\r\n%s",
										  ok, strlen(user_agent), user_agent);
				send(client_fd, response, full_length, 0);
				break;
			}
		} while (line);
	} else {
		const char *response = "HTTP/1.1 404 Not Found\r\n"
							   "Content-Length: 0";
		send(client_fd, response, strlen(response), 0);
	}

	send(client_fd, "\r\n\r\n", 4, 0);
}

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be visible
	// when running tests.
	printf("Logs from your program will appear here!\n");

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
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
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	while (1) {
		const int client_fd = accept(server_fd, (struct sockaddr *)&client_addr,
									 &client_addr_len);
		handle_client(client_fd);
	}
	printf("Client connected\n");

	close(server_fd);

	return 0;
}
