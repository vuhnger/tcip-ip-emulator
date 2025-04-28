#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "l2sap.h"

/* compute_checksum is a helper function for l2_sendto and
 * l2_recvfrom_timeout to compute the 1-byte checksum both
 * on sending and receiving and L2 frame.
 */
static uint8_t compute_checksum(const uint8_t *frame, int len)
{
    uint8_t checksum = 0;
    for (int i = 0; i < len; ++i)
    {
        checksum ^= frame[i];
    }
    return checksum;
}

L2SAP *l2sap_create(const char *server_ip, int server_port)
{
    L2SAP *service_access_point = malloc(sizeof(struct L2SAP));
    if (!service_access_point)
    {
        fprintf(stderr, "%s: failed to allocate memory for service_access_point.\n", __FUNCTION__);
        return NULL;
    }

    service_access_point->socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (service_access_point->socket < 0)
    {
        fprintf(stderr, "%s: failed to create socket.\n", __FUNCTION__);
        free(service_access_point);
        return NULL;
    }

    memset(&service_access_point->peer_addr, 0, sizeof(service_access_point->peer_addr));
    service_access_point->peer_addr.sin_family = AF_INET;
    service_access_point->peer_addr.sin_port = htons(server_port);

    int validIp = inet_pton(AF_INET, server_ip, &service_access_point->peer_addr.sin_addr);
    if (validIp <= 0)
    {
        fprintf(stderr, "%s: Invalid IP address\n", __FUNCTION__);
        close(service_access_point->socket);
        free(service_access_point);
        return NULL;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;

    int bindValue = bind(service_access_point->socket, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (bindValue < 0)
    {
        fprintf(stderr, "%s: binding failed\n", __FUNCTION__);
        close(service_access_point->socket);
        free(service_access_point);
        return NULL;
    }
    fprintf(stderr, "%s: bound socket to address %s\n", __FUNCTION__, inet_ntoa(local_addr.sin_addr));

    return service_access_point;
}

void l2sap_destroy(L2SAP *client)
{
    if (client == NULL)
    {
        fprintf(stderr, "%s: client was null\n", __FUNCTION__);
        return;
    }

    if (client->socket >= 0)
    {
        fprintf(stderr, "%s: closing socket\n", __FUNCTION__);
        close(client->socket);
    }

    fprintf(stderr, "%s: freeing client memory\n", __FUNCTION__);
    free(client);
}

/* l2sap_sendto sends data over UDP, using the given UDP socket
 * sock, to a remote UDP receiver that is identified by
 * peer_address.
 * The parameter data points to payload that L3 wants to send
 * to the remote L3 entity. This payload is len bytes long.
 * l2_sendto must add an L2 header in front of this payload.
 * When the payload length and the L2Header together exceed
 * the maximum frame size L2Framesize, l2_sendto fails.
 */
int l2sap_sendto(L2SAP *client, const uint8_t *data, int len)
{
    if (client == NULL || data == NULL || len < 0)
    {
        fprintf(stderr, "%s: invalid parameters\n", __FUNCTION__);
        return -1;
    }

    if (len + sizeof(L2Header) > L2Framesize)
    {
        fprintf(stderr, "%s: payload is too large\n", __FUNCTION__);
        return -1;
    }

    uint8_t frame[L2Framesize];
    L2Header *header = (L2Header *)frame;

    const int PACKET_SIZE = len + sizeof(L2Header);

    header->dst_addr = client->peer_addr.sin_addr.s_addr;
    header->len = htons(PACKET_SIZE);
    header->checksum = 0;
    header->mbz = 0;
    memcpy(frame + sizeof(L2Header), data, len);
    uint8_t temp_checksum = compute_checksum(frame, sizeof(L2Header) + len);
    header->checksum = temp_checksum;

    fprintf(stderr, "%s: Size of payload+headerr: %d\n", __FUNCTION__, PACKET_SIZE);

    int bytes_sent = sendto(client->socket, frame, PACKET_SIZE, 0,
                            (struct sockaddr *)&client->peer_addr, sizeof(client->peer_addr));

    if (bytes_sent < 0)
    {
        fprintf(stderr, "%s: fail to send bytes, sent %d.\n", __FUNCTION__, bytes_sent);
        return -1;
    }
    if (bytes_sent != PACKET_SIZE)
    {
        fprintf(stderr, "%s: sent %d bytes,  expected %d\n", __FUNCTION__, bytes_sent, PACKET_SIZE);
        return -1;
    }

    fprintf(stderr, "%s Sending frame of size %d to %s:%d\n", __FUNCTION__, bytes_sent,
            inet_ntoa(client->peer_addr.sin_addr), ntohs(client->peer_addr.sin_port));

    return len;
}

/* Convenience function. Calls l2sap_recvfrom_timeout with NULL timeout
 * to make it waits endlessly.
 */
int l2sap_recvfrom(L2SAP *client, uint8_t *data, int len)
{
    return l2sap_recvfrom_timeout(client, data, len, NULL);
}

/* l2sap_recvfrom_timeout waits for data from a remote UDP sender, but
 * waits at most timeout seconds.
 * It is possible to pass NULL as timeout, in which case
 * the function waits forever.
 *
 * If a frame arrives in the meantime, it stores the remote
 * peer's address in peer_address and its size in peer_addr_sz.
 * After removing the header, the data of the frame is stored
 * in data, up to len bytes.
 *
 * If data is received, it returns the number of bytes.
 * If no data is reveid before the timeout, it returns L2_TIMEOUT,
 * which has the value 0.
 * It returns -1 in case of error.
 */
int l2sap_recvfrom_timeout(L2SAP *client, uint8_t *data, int len, struct timeval *timeout)
{
    if (client == NULL || data == NULL || len <= 0)
    {
        fprintf(stderr, "%s: invalid parameters.\n", __FUNCTION__);
        return -1;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->socket, &readfds);

    struct timeval timeout_copy;
    if (timeout != NULL)
    {
        timeout_copy = *timeout;
        fprintf(stderr, "%s: setting timeout to %ld\n", __FUNCTION__, timeout->tv_sec);
    }

    int select_result = select(client->socket + 1, &readfds, NULL, NULL, timeout ? &timeout_copy : NULL);

    fprintf(stderr, "%s: seleted result is %d\n", __FUNCTION__, select_result);

    if (select_result < 0)
    {
        fprintf(stderr, "%s: select call failed\n", __FUNCTION__);
        return -1;
    }

    if (select_result == 0)
    {
        fprintf(stderr, "%s: L2_TIMEOUT\n", __FUNCTION__);
        return L2_TIMEOUT;
    }

    uint8_t frame[L2Framesize];

    struct sockaddr_in sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);

    int bytes_received = recvfrom(client->socket, frame, L2Framesize, 0,
                                  (struct sockaddr *)&sender_addr, &sender_addr_len);

    if (bytes_received < 0)
    {
        fprintf(stderr, "%s: recvfrom call failed\n", __FUNCTION__);
        return -1;
    }

    if (bytes_received < sizeof(L2Header))
    {
        fprintf(stderr, "%s: received frame too small (%d bytes)\n",
                __FUNCTION__, bytes_received);
        return -1;
    }

    fprintf(stderr, "%s: recieved %d bytes\n", __FUNCTION__, bytes_received);

    L2Header *header = (L2Header *)frame;

    int total_len = ntohs(header->len);

    int payload_len = total_len - sizeof(L2Header);

    if (payload_len < 0)
    {
        fprintf(stderr, "%s: invalid payload length (%d)\n", __FUNCTION__, payload_len);
        return -1;
    }

    if (total_len != bytes_received)
    {
        fprintf(stderr, "%s: header indicates total size %d, but received %d\n",
                __FUNCTION__,
                total_len,
                (int)(bytes_received));

        payload_len = bytes_received - sizeof(L2Header);

        if (payload_len < 0)
        {
            fprintf(stderr, "%s: calculated negative payload length\n", __FUNCTION__);
            return -1;
        }
    }

    fprintf(stderr, "%s: payload length (packet size - header size) is %d\n", __FUNCTION__, payload_len);

    uint8_t received_checksum = header->checksum;
    header->checksum = 0;

    uint8_t calculated_checksum = compute_checksum(frame, bytes_received);

    if (calculated_checksum != received_checksum)
    {
        fprintf(stderr, "%s: checksum verification failed (got %d, expected %d)\n",
                __FUNCTION__, calculated_checksum, received_checksum);
        return -1;
    }

    client->peer_addr = sender_addr;

    int copy_len;

    if (payload_len < len)
    {
        copy_len = payload_len;
    }
    else
    {
        copy_len = len;
    }

    if (copy_len > 0)
    {
        memcpy(data, frame + sizeof(L2Header), copy_len);
    }

    return copy_len;
}