//
// Created by Dinuja Gunawardana on 2023-11-08.
//
/*
 * This code is licensed under the Attribution-NonCommercial-NoDerivatives 4.0
 * International license.
 *
 * Author: D'Arcy Smith (ds@programming101.dev)
 *
 * You are free to:
 *   - Share: Copy and redistribute the material in any medium or format.
 *   - Under the following terms:
 *       - Attribution: You must give appropriate credit, provide a link to the
 * license, and indicate if changes were made.
 *       - NonCommercial: You may not use the material for commercial purposes.
 *       - NoDerivatives: If you remix, transform, or build upon the material,
 * you may not distribute the modified material.
 *
 * For more details, please refer to the full license text at:
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void setup_signal_handler(void);

static void sigint_handler(int signum);

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port);

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port);

static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);

_Noreturn static void usage(const char *program_name, int exit_code, const char *message);

static void convert_address(const char *address, struct sockaddr_storage *addr);

static int socket_create(int domain, int type, int protocol);

static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);

static void start_listening(int server_fd);

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);

static void handle_connection(int client_sockfd);

static void socket_close(int sockfd);

void execCommand(char *command);

char *findBinaryInPath(const char *command);

void parseCommand(char *command, char *tokens[], int *token_count);

#define BASE_TEN 10
#define SIZE 256
#define BUFFER 1024
#define HUNDRED 100

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[])
{
    char                   *address;
    char                   *port_str;
    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;

    address  = NULL;
    port_str = NULL;
    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(argv[0], address, port_str, &port);
    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_bind(sockfd, &addr, port);
    start_listening(sockfd);
    setup_signal_handler();

    while(exit_flag == 0)
    {
        int                     client_sockfd;
        struct sockaddr_storage client_addr;
        socklen_t               client_addr_len;

        client_addr_len = sizeof(client_addr);
        client_sockfd   = socket_accept_connection(sockfd, &client_addr, &client_addr_len);

        if(client_sockfd == -1)
        {
            if(exit_flag)
            {
                break;
            }

            continue;
        }

        handle_connection(client_sockfd);
    }

    socket_close(sockfd);

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port)
{
    if(optind >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The ip address and port are required");
    }

    if(optind + 1 >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The port is required");
    }

    if(optind < argc - 3)
    {
        usage(argv[0], EXIT_FAILURE, "Error: Too many arguments.");
    }

    *ip_address = argv[1];
    *port       = argv[2];
}

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port)
{
    if(ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The ip address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s <ip address> <port>\n", program_name);
    exit(exit_code);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void sigint_handler(int signum)
{
    exit_flag = 1;
}

#pragma GCC diagnostic pop

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr,
                "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: "
                "%d\n",
                addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

static void start_listening(int server_fd)
{
    if(listen(server_fd, SOMAXCONN) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];

    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed");
        }

        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}

static void setup_signal_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void handle_connection(int client_sockfd)
{
    char    message[BUFFER];
    ssize_t received;
    int     clientSTDOUT;
    memset(message, 0, sizeof(message));

    received = read(client_sockfd, message, sizeof(message));

    if(received == 0)
    {
        // Connection closed gracefully
        printf("Connection closed by the client\n");
    }
    else if(received < 0)
    {
        // Error reading from the socket
        perror("Error reading from socket");
        // Handle the error or exit the program.
    }
    else
    {
        // Data was successfully read
        message[received] = '\0';
        printf("Message received: %s\n", message);
    }

    clientSTDOUT = dup2(client_sockfd, STDOUT_FILENO);

    if(clientSTDOUT == -1)
    {
        perror("Missing");
        exit(EXIT_FAILURE);
    }

    execCommand(message);
    close(client_sockfd);
}

#pragma GCC diagnostic pop

static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        fprintf(stderr, "Socket close error: sockfd=%d\n", sockfd);
        exit(EXIT_FAILURE);
    }
}

void parseCommand(char *command, char *tokens[], int *token_count)
{
    char *saveptr;
    char *token;

    token        = strtok_r(command, " ", &saveptr);
    *token_count = 0;

    while(token != NULL)
    {
        tokens[(*token_count)++] = strdup(token);
        token                    = strtok_r(NULL, " ", &saveptr);
    }

    tokens[*token_count] = NULL;
}

char *findBinaryInPath(const char *command)
{
    char *path;
    char *path_copy = NULL;    // Initialize to NULL
    char *dir;
    char  binary_path[SIZE];
    char *saveptr;

    path = getenv("PATH");

    if(path != NULL)
    {
        path_copy = strdup(path);
        if(path_copy == NULL)
        {
            perror("strdup");
            return NULL;    // Handle strdup failure by returning NULL
        }
        dir = strtok_r(path_copy, ":", &saveptr);
    }
    else
    {
        perror("getenv");
        return NULL;    // Handle missing PATH environment variable by returning NULL
    }

    while(dir != NULL)
    {
        snprintf(binary_path, sizeof(binary_path), "%s/%s", dir, command);

        if(access(binary_path, X_OK) == 0)
        {
            free(path_copy);
            return strdup(binary_path);
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    return NULL;    // Return NULL if the binary is not found in any of the
    // directories
}

void execCommand(char *command)
{
    char *tokens[HUNDRED];
    int   token_count;
    char *binary_path;
    pid_t child_pid;

    parseCommand(command, tokens, &token_count);

    if(token_count == 0)
    {
        fprintf(stderr, "No command provided.\n");
        exit(EXIT_FAILURE);
    }

    binary_path = findBinaryInPath(tokens[0]);

    if(binary_path == NULL)
    {
        fprintf(stderr, "Command not found: %s\n", tokens[0]);
        exit(EXIT_FAILURE);
    }

    child_pid = fork();

    if(child_pid == -1)
    {
        perror("Fork failed");
        free(binary_path);
        exit(EXIT_FAILURE);
    }
    else if(child_pid == 0)
    {
        // Child process
        execv(binary_path, tokens);
        perror("Execv failed");
        exit(1);
    }
    else
    {
        // Parent process
        int status;
        waitpid(child_pid, &status, 0);

        if(WIFEXITED(status))
        {
            printf("Child process exited with status %d\n", WEXITSTATUS(status));
        }
        else
        {
            printf("Child process did not exit normally.\n");
        }
    }

    free(binary_path);
    for(int i = 0; i < token_count; i++)
    {
        free(tokens[i]);    // Free the memory allocated for tokens
    }
}
