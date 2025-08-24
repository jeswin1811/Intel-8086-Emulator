#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/cpu.h"
#include "../include/memory.h"

#define SERVER_PORT 5555
#define BACKLOG 1

static int recv_all(SOCKET sock, void *buf, size_t len) {
    size_t received = 0;
    char *p = (char*)buf;
    while (received < len) {
#ifdef _WIN32
        int r = recv(sock, p + received, (int)(len - received), 0);
#else
        ssize_t r = recv(sock, p + received, len - received, 0);
#endif
        if (r <= 0) return 0;
        received += r;
    }
    return 1;
}

static int send_all(SOCKET sock, const void *buf, size_t len) {
    size_t sent = 0;
    const char *p = (const char*)buf;
    while (sent < len) {
#ifdef _WIN32
        int s = send(sock, p + sent, (int)(len - sent), 0);
#else
        ssize_t s = send(sock, p + sent, len - sent, 0);
#endif
        if (s <= 0) return 0;
        sent += s;
    }
    return 1;
}

int main(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

#ifdef _WIN32
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (listen_sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    int yes = 1;
#ifdef _WIN32
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_sock, BACKLOG) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "emu_server listening on port %d\n", SERVER_PORT);
    FILE *logfile = fopen("emu_server.log", "w");
    fprintf(logfile, "emu_server listening on port %d\n", SERVER_PORT);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client = accept(listen_sock, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if (client < 0) { perror("accept"); break; }
        fprintf(stderr, "client connected\n");

        // Read 4-byte little-endian size
        uint32_t size = 0;
        if (!recv_all(client, &size, sizeof(size))) { fprintf(stderr, "failed read size\n");
#ifdef _WIN32
            closesocket(client);
#else
            close(client);
#endif
            continue;
        }

        // Limit size to 64KB - 256 for safety
        if (size > 65536) size = 65536;
        uint8_t *buf = malloc(size);
        if (!buf) { fprintf(stderr, "alloc failed\n");
#ifdef _WIN32
            closesocket(client);
#else
            close(client);
#endif
            continue;
        }
        if (!recv_all(client, buf, size)) { fprintf(stderr, "failed read payload\n"); free(buf);
#ifdef _WIN32
            closesocket(client);
#else
            close(client);
#endif
            continue;
        }

        // Setup memory and cpu
        Memory8086 mem;
        memset(&mem, 0, sizeof(mem));
        CPU8086 cpu;
        cpu_init(&cpu);

        // Load into 0x100
        if (size > 0) memcpy(&mem.data[0x100], buf, size);
        free(buf);

        cpu.cs = 0x0000;
        cpu.ip = 0x0100;

        // Reset output buffer
        emu_out_pos = 0; emu_output[0] = 0;

        // Run until exit
        while (cpu_step(&cpu, &mem)) {}
    // Debug: report output length to server stderr
    fprintf(stderr, "emu_out_pos=%u\n", (unsigned)emu_out_pos);

    // Send back output length (4 bytes LE) then output
        uint32_t out_len = (uint32_t)emu_out_pos;
        if (!send_all(client, &out_len, sizeof(out_len))) { fprintf(stderr, "send len failed\n"); }
        if (out_len > 0) {
            if (!send_all(client, emu_output, out_len)) { fprintf(stderr, "send data failed\n"); }
        }

#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
        fprintf(stderr, "client done\n");
    }

#ifdef _WIN32
    closesocket(listen_sock);
    WSACleanup();
#else
    close(listen_sock);
#endif
    return 0;
}
