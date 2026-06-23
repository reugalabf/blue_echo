#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

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

        buf[bytes_read] = '\0';
        printf("Received: %s", buf);

        // Echo back to client
        echo = malloc(bytes_read +1);
        sprintf(echo,"!%s",buf);
        if (write(client_sock, echo, bytes_read+1) != bytes_read+1) {
            perror("write");
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

