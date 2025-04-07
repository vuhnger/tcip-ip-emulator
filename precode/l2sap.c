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
static uint8_t compute_checksum( const uint8_t* frame, int len ){
    uint8_t checksum = 0;

    for (int i = 0; i < len; ++i){
        checksum ^=frame[i];
    }

    return checksum;
}

L2SAP* l2sap_create( const char* server_ip, int server_port )
{

    L2SAP * service_access_point = malloc(sizeof(struct L2SAP));
    if (!service_access_point){
        fprintf(stderr, "L2SAP: failed to allocate memory for service_access_point.\n");
        return NULL;
    }

    service_access_point->socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (service_access_point->socket < 0){
        fprintf(stderr, "L2SAP: failed to create socket.\n");
        free(service_access_point);
        return NULL;
    }

    memset(&service_access_point->peer_addr, 0, sizeof(service_access_point->peer_addr));
    service_access_point->peer_addr.sin_family = AF_INET;
    service_access_point->peer_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &service_access_point->peer_addr.sin_addr) <= 0){
        fprintf(stderr, "L2SAP: Invalid IP address.\n");
        close(service_access_point->socket);
        free(service_access_point);
        return NULL;
    }

    if (bind(service_access_point->socket, (struct sockaddr*) &service_access_point->peer_addr, sizeof(service_access_point->peer_addr)) < 0){
        fprintf(stderr, "L2SAP: binding failed.\n");
        close(service_access_point->socket);
        free(service_access_point);
        return NULL;
    }

    return service_access_point;
}

void l2sap_destroy(L2SAP* client)
{
    if (client == NULL){
        return;
    }

    if (client->socket >= 0){
        close(client->socket);
    }

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
int l2sap_sendto( L2SAP* client, const uint8_t* data, int len )
{
    if (client == NULL || data == NULL || len < 0){
        fprintf(stderr, "L2SAP_sendto: Invalid parameters.\n");
        return -1;
    }

    if (len + sizeof(L2Header) > L2Framesize){
        fprintf(stderr, "L2SAP_sendto: Payload is too large!\n");
        return -1;
    }

    uint8_t frame[L2Framesize];
    L2Header* header = (L2Header*) frame;
    
    header->dst_addr = client->peer_addr.sin_addr.s_addr;
    header->len = htons(len);
    header-> checksum = 0; // Initialize to 0 and compute checksum value later
    header->mbz = 0;

    memcpy(frame + sizeof(L2Header), data, len);
    header->checksum = compute_checksum(frame, sizeof(L2Header) + len);

    int bytes_sent = sendto(
        client->socket,
        frame,
        sizeof(L2Header) + len,
        0,
        (struct sockaddr*)&client->peer_addr,sizeof(client->peer_addr)
    );

    if (bytes_sent < 0 ){
        fprintf(stderr, "L2SAP_sendto: failed to sent bytes.\n");
        return -1;
    }

    if (bytes_sent != sizeof(L2Header) + len){
        fprintf(stderr, "L2SAP_sendto: sent %d bytes but expected %d\n", bytes_sent, (int) (sizeof(L2Header) + len));
        return -1;
    }

    return len;
}

/* Convenience function. Calls l2sap_recvfrom_timeout with NULL timeout
 * to make it waits endlessly.
 */
int l2sap_recvfrom( L2SAP* client, uint8_t* data, int len )
{
    return l2sap_recvfrom_timeout( client, data, len, NULL );
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
int l2sap_recvfrom_timeout(L2SAP* client, uint8_t* data, int len, struct timeval* timeout)
{

    if (client == NULL || data == NULL || len <= 0) {
        fprintf(stderr, "L2SAP_recvfrom_timeout: Invalid parameters.\n");
        return -1;
    }
    

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->socket, &readfds);
    

    struct timeval timeout_copy;
    if (timeout != NULL) {
        timeout_copy = *timeout;
    }
    

    int select_result = select(client->socket + 1, &readfds, NULL, NULL, 
                              timeout ? &timeout_copy : NULL);
    
    if (select_result < 0) {
        fprintf(stderr, "L2SAP_recvfrom_timeout: select call failed\n");
        return -1;
    }
    
    if (select_result == 0) {
        return L2_TIMEOUT;
    }
    
    
    uint8_t frame[L2Framesize];
    
    
    struct sockaddr_in sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);
    
    int bytes_received = recvfrom(client->socket, frame, L2Framesize, 0,
                                 (struct sockaddr*)&sender_addr, &sender_addr_len);
    
    if (bytes_received < 0) {
        fprintf(stderr, "L2SAP_recvfrom_timeout: recvfrom call failed\n");
        return -1;
    }
    
    
    if (bytes_received < sizeof(L2Header)) {
        fprintf(stderr, "L2SAP_recvfrom_timeout: received frame too small (%d bytes)\n", 
                bytes_received);
        return -1;
    }
    
    L2Header* header = (L2Header*)frame;
    
    
    int payload_len = ntohs(header->len);
    
    
    if (sizeof(L2Header) + payload_len != bytes_received) {
        fprintf(stderr, "L2SAP_recvfrom_timeout: header indicates payload size %d, but received %d\n",
                payload_len, bytes_received - sizeof(L2Header));
        return -1;
    }
    
    // Verify checksum
    uint8_t received_checksum = header->checksum;
    header->checksum = 0;  // Zero out for checksum calculation
    
    uint8_t calculated_checksum = compute_checksum(frame, bytes_received);
    
    if (calculated_checksum != received_checksum) {
        fprintf(stderr, "L2SAP_recvfrom_timeout: checksum verification failed (got %02x, expected %02x)\n",
                calculated_checksum, received_checksum);
        return -1;
    }
    
    
    client->peer_addr = sender_addr;
    
    
    int copy_len = payload_len < len ? payload_len : len;
    memcpy(data, frame + sizeof(L2Header), copy_len);
    
    
    return copy_len;
}

