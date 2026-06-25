#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

int write_all(int fd, const uint8_t *buf, int len) {
    int total_written = 0;
    while (total_written < len) {
        int written = write(fd, buf + total_written, len - total_written);
        if (written <= 0) {
            perror("write");
            return -1;
        }
        total_written += written;
    }
    return 0;
}

int main() {
    int server_sock = -1, client_sock = -1;
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    socklen_t opt = sizeof(rem_addr);
    char buf[1024] = { 0 };
    char *echo;
    int bytes_read;

    // 1. Allocate socket
    server_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }

    // 2. Bind socket to port 1 (standard RFCOMM channel)
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = (bdaddr_t) {0}; // Any local adapter
    loc_addr.rc_channel = (uint8_t) 1;

    if (bind(server_sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
        perror("bind");
        goto cleanup;
    }

    // 3. Put socket into listening mode
    if (listen(server_sock, 1) < 0) {
        perror("listen");
        goto cleanup;
    }

    printf("Bluetooth Echo Server is waiting for connection on channel %d...\n", loc_addr.rc_channel);

    // 4. Accept one incoming connection
    client_sock = accept(server_sock, (struct sockaddr *)&rem_addr, &opt);
    if (client_sock < 0) {
        perror("accept");
        goto cleanup;
    }

    // Convert and print the connected device's MAC address
    char client_mac[19] = { 0 };
    ba2str(&rem_addr.rc_bdaddr, client_mac);
    printf("Accepted connection from %s\n", client_mac);

    // 5. Read data and echo it back
    while (1) {
        bytes_read = read(client_sock, buf, sizeof(buf));
        if (bytes_read <= 0) {
            printf("Client disconnected or read error.\n");
            break;
        }

        buf[sizeof(buf) - 1] = '\0';
        printf("Received: %s", buf);

        // Echo back to client
        echo = malloc(bytes_read +1);
        if (!echo) { perror("malloc"); break; }

        sprintf(echo,"!%s",buf);
        if (write_all(client_sock, (uint8_t *)echo, bytes_read + 1) < 0) {
    break;
}
        free(echo);
    }

cleanup:
    // 6. Close sockets
    if (client_sock >= 0) close(client_sock);
    if (server_sock >= 0) close(server_sock);
    return 0;
}

