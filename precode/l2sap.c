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
static uint8_t compute_checksum( const uint8_t* frame, int len );

L2SAP* l2sap_create( const char* server_ip, int server_port )
{

    L2SAP * link_layer_state = malloc(sizeof(struct L2SAP));
    if (!link_layer_state){
        fprintf(stderr, "L2SAP: failed to allocate memory for link_layer_state.\n");
        return NULL;
    }

    link_layer_state->socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (link_layer_state->socket < 0){
        fprintf(stderr, "L2SAP: failed to create socket.\n");
        free(link_layer_state);
        return NULL;
    }

    memset(&link_layer_state->peer_addr, 0, sizeof(link_layer_state->peer_addr));
    link_layer_state->peer_addr.sin_family = AF_INET;
    link_layer_state->peer_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &link_layer_state->peer_addr.sin_addr) <= 0){
        fprintf(stderr, "L2SAP: Invalid IP address.\n");
        close(link_layer_state->socket);
        free(link_layer_state);
        return NULL;
    }

    if (bind(link_layer_state->socket, (struct sockaddr*) &link_layer_state->peer_addr, sizeof(link_layer_state->peer_addr)) < 0){
        fprintf(stderr, "L2SAP: binding failed.\n");
        close(link_layer_state->socket);
        free(link_layer_state);
        return NULL;
    }

    return link_layer_state;
}

void l2sap_destroy(L2SAP* client)
{
    fprintf( stderr, "%s has not been implemented yet\n", __FUNCTION__ );
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
    fprintf( stderr, "%s has not been implemented yet\n", __FUNCTION__ );
    return -1;
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
int l2sap_recvfrom_timeout( L2SAP* client, uint8_t* data, int len, struct timeval* timeout )
{
    fprintf( stderr, "%s has not been implemented yet\n", __FUNCTION__ );
    return -1;
}

