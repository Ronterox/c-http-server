#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define FILE_LIMIT 100
#define CONNECTION_BACKLOG 10
#define END_OF_REQUEST "\r\n\r\n"

const char ok[] = "HTTP/1.1 200 OK\r\n";
const char not_found[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";

struct client_args {
	int client_fd;
	char *directory;
	char (*files)[256];
	int file_count;
};

void get_root(int *client_fd) {
	const char *response = "HTTP/1.1 200 OK\r\n"
						   "Content-Length: 0" END_OF_REQUEST;
	send(*client_fd, response, strlen(response), 0);
}

void echo(int *client_fd, char *body) {
	char response[1024];
	int full_length = sprintf(response,
							  "%sContent-Type: text/plain\r\n"
							  "Content-Length: %ld\r\n\r\n%s" END_OF_REQUEST,
							  ok, strlen(body), body);
	send(*client_fd, response, full_length, 0);
}

void user_agent(int *client_fd) {
	char *line;
	do {
		line = strtok(NULL, "\r\n");
		if (strncmp(line, "User-Agent: ", 12) == 0) {
			char *user_agent = line + 12;
			char response[1024];
			int full_length =
				sprintf(response,
						"%sContent-Type: text/plain\r\n"
						"Content-Length: %ld\r\n\r\n%s" END_OF_REQUEST,
						ok, strlen(user_agent), user_agent);
			send(*client_fd, response, full_length, 0);
			break;
		}
	} while (line);
}

void get_file(struct client_args *client_args, char *path) {
	for (int i = 0; i < client_args->file_count; i++) {
		char *filename = client_args->files[i];
		printf("Filename: %s, Path: %s\n", filename, path + 7);
		if (strcmp(path + 7, filename) == 0) {
			char filepath[256];
			sprintf(filepath, "%s/%s", client_args->directory, filename);

			FILE *file = fopen(filepath, "r");
			if (!file) {
				printf("File not found: %s\n", strerror(errno));
				return;
			}

			fseek(file, 0, SEEK_END);
			long file_size = ftell(file);

			char *file_contents = malloc(file_size);
			fseek(file, 0, SEEK_SET);
			fread(file_contents, 1, file_size, file);

			printf("File size: %ld\n", file_size);
			char *response = malloc(1024 + file_size);
			int full_length =
				sprintf(response,
						"%sContent-Type: application/octet-stream\r\n"
						"Content-Length: %ld\r\n\r\n%s" END_OF_REQUEST,
						ok, file_size, file_contents);
			send(client_args->client_fd, response, full_length, 0);
			free(file_contents);
			free(response);
			fclose(file);
			return;
		}
	}
	send(client_args->client_fd, not_found, strlen(not_found), 0);
}

void set_file(struct client_args *client_args, char *body, char *path) {
	char filepath[256];
	sprintf(filepath, "%s/%s", client_args->directory, path + 7);

	FILE *file = fopen(filepath, "w");
	body = strtok(body, "\r\n");
	fprintf(file, "%s", body);

	char *response = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
	send(client_args->client_fd, response, strlen(response), 0);
	fclose(file);
}

void *handle_client(void *args) {
	struct client_args *client_args = (struct client_args *)args;
	int client_fd = client_args->client_fd;

	char request[1024];
	if (read(client_fd, request, sizeof(request)) < 0) {
		printf("Read failed: %s \n", strerror(errno));
		close(client_fd);
		return NULL;
	}

	printf("Received request:\n%s\n", request);
	char *body = strstr(request, "\r\n\r\n");
	char *method = strtok(request, " ");
	char *path = strtok(NULL, " ");
	if (!path) {
		const char *response = "HTTP/1.1 400 Bad Request\r\n"
							   "Content-Length: 0" END_OF_REQUEST;
		send(client_fd, response, strlen(response), 0);
		close(client_fd);
		return NULL;
	}

	if (strcmp(path, "/") == 0) {
		get_root(&client_fd);
	} else if (strncmp(path, "/echo/", 6) == 0) {
		char *body = strlen(path) > 6 ? path + 6 : "";
		echo(&client_fd, body);
	} else if (strcmp(path, "/user-agent") == 0) {
		user_agent(&client_fd);
	} else if (strncmp(path, "/files/", 7) == 0) {
		if (strcmp(method, "GET") == 0)
			get_file(client_args, path);
		else if (strcmp(method, "POST") == 0)
			set_file(client_args, body, path);
	} else {
		send(client_fd, not_found, strlen(not_found), 0);
	}
	close(client_fd);
	return NULL;
}

int setup_server(int *server_fd) {
	*server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (*server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting
	// REUSE_PORT ensures that we don't run into 'Address already in use'
	// errors
	const int reuse = 1;
	if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse,
				   sizeof(reuse)) < 0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(*server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) !=
		0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Listening on http://localhost:4221\n");
	if (listen(*server_fd, CONNECTION_BACKLOG) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	return 0;
}

char *set_directory(int *argc, char *argv[]) {
	int opt, last;
	char *directory = ".";
	const static struct option long_options[] = {
		{"directory", required_argument, 0, 'd'}};

	while ((opt = getopt_long(*argc, argv, "d:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			last = strlen(optarg) - 1;
			if (optarg[last] == '/') {
				optarg[last] = '\0';
			}
			directory = optarg;
			break;
		default:
			printf("Usage: %s --directory <path>\n", argv[0]);
			return NULL;
		}
	}

	return directory;
}

int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be
	// visible when running tests.
	printf("Logs from your program will appear here!\n");

	char *directory = set_directory(&argc, argv);
	if (!directory) {
		return 1;
	}
	printf("Directory: %s\n", directory);

	DIR *dir = opendir(directory);
	if (!dir) {
		printf("Directory not found: %s\n", strerror(errno));
		return 1;
	}

	struct dirent *entry;
	char files[FILE_LIMIT][256];
	int file_count = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG) {
			printf("File: %s\n", entry->d_name);
			strcpy(files[file_count], entry->d_name);
			file_count++;
		}
	}
	printf("File count: %d\n", file_count);
	closedir(dir);

	int server_fd;
	if (setup_server(&server_fd) != 0) {
		return 1;
	}

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	while (1) {
		printf("Waiting for a client to connect...\n");
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr,
							   &client_addr_len);
		if (client_fd < 0) {
			printf("Accept failed: %s \n", strerror(errno));
			return 1;
		}
		printf("Client %d connected\n", client_fd);

		pthread_t thread;
		struct client_args args = {client_fd, directory, files, file_count};
		if (pthread_create(&thread, NULL, handle_client, &args) != 0) {
			printf("Thread creation failed: %s \n", strerror(errno));
			return 1;
		}
		pthread_detach(thread);
	}

	for (int i = 0; i < file_count; i++) {
		free(files[i]);
	}
	close(server_fd);

	return 0;
}
