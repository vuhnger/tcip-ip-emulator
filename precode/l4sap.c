#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "l4sap.h"
#include "l2sap.h"

/* Create an L4 client.
 * It returns a dynamically allocated struct L4SAP that contains the
 * data of this L4 entity (including the pointer to the L2 entity
 * used).
 */
L4SAP *l4sap_create(const char *server_ip, int server_port)
{
    fprintf(stderr, "DEBUG: l4sap_create called with server_ip=%s, port=%d\n",
            server_ip, server_port);

    if (server_ip == NULL || server_port <= 0)
    {
        fprintf(stderr, "L4SAP: Invalid parameters.\n");
        return NULL;
    }

    // Allocate memory for L4SAP structure
    fprintf(stderr, "DEBUG: Allocating L4SAP structure\n");
    L4SAP *l4 = malloc(sizeof(L4SAP));
    if (!l4)
    {
        fprintf(stderr, "L4SAP: failed to allocate memory for L4SAP.\n");
        return NULL;
    }

    // Create L2SAP instance
    fprintf(stderr, "DEBUG: Creating L2SAP instance\n");
    l4->l2 = l2sap_create(server_ip, server_port);
    if (l4->l2 == NULL)
    {
        fprintf(stderr, "L4SAP: failed to create L2SAP.\n");
        l2sap_destroy(l4->l2);
        free(l4);
        return NULL;
    }

    // Initialize L4SAP fields
    fprintf(stderr, "DEBUG: Initializing L4SAP fields\n");
    l4->next_send_seq = 0;
    l4->expected_recv_seq = 0;
    l4->is_terminating = 0;
    l4->send_state.length = 0;
    l4->send_state.last_ack_recieved = 0;
    l4->recv_state.last_seqno_recieved = 0;
    l4->recv_state.last_ack_sent = 0;

    fprintf(stderr, "DEBUG: Initializing send_state buffer\n");
    // Check that send_state.buffer exists before using memset
    fprintf(stderr, "DEBUG: Size of buffer = %zu\n", sizeof(l4->send_state.buffer));

    // Initialize send_state buffer
    memset(l4->send_state.buffer, 0, sizeof(l4->send_state.buffer));

    // Initialize L4Header
    fprintf(stderr, "DEBUG: Setting up L4Header\n");
    L4Header *header = (L4Header *)l4->send_state.buffer;
    header->type = 0;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->expected_recv_seq;
    header->mbz = 0;

    fprintf(stderr, "DEBUG: l4sap_create completed successfully\n");
    return l4;
}

/* The functions sends a packet to the network. The packet's payload
 * is copied from the buffer that it is passed as an argument from
 * the caller at L5.
 * If the length of that buffer, which is indicated by len, is larger
 * than L4Payloadsize, the function truncates the message to L4Payloadsize.
 *
 * The function does not return until the correct ACK from the peer entity
 * has been received.
 * When a suitable ACK arrives, the function returns the number of bytes
 * that were accepted for sending (the potentially truncated packet length).
 *
 * Waiting for a correct ACK may fail after a timeout of 1 second
 * (timeval.tv_sec = 1, timeval.tv_usec = 0). The function retransmits
 * the packet in that case.
 * The function attempts up to 4 retransmissions. If the last retransmission
 * fails with a timeout as well, the function returns L4_SEND_FAILED.
 *
 * The function may also return:
 * - L4_QUIT if the peer entity has sent an L4_RESET packet.
 * - another value < 0 if an error occurred.
 */
int l4sap_send(L4SAP *l4, const uint8_t *data, int len)
{
    if (l4 == NULL || data == NULL || len < 0)
    {
        fprintf(stderr, "L4SAP_send: Invalid parameters.\n");
        return -1;
    }



    return L4_QUIT;
}

/* The functions receives a packet from the network. The packet's
 * payload is copy into the buffer that it is passed as an argument
 * from the caller at L5.
 * The function blocks endlessly, meaning that experiencing a timeout
 * does not terminate this function.
 * The function returns the number of bytes copied into the buffer
 * (only the payload of the L4 packet).
 * The function may also return:
 * - L4_QUIT if the peer entity has sent an L4_RESET packet.
 * - another value < 0 if an error occurred.
 */
int l4sap_recv(L4SAP *l4, uint8_t *data, int len)
{
    fprintf(stderr, "%s has not been implemented yet\n", __FUNCTION__);
    return L4_QUIT;
}

/** This function is called to terminate the L4 entity and
 *  free all of its resources.
 *  We recommend that you send several L4_RESET packets from
 *  this function to ensure that the peer entity is also
 *  terminating correctly.
 */
void l4sap_destroy(L4SAP *l4)
{
    fprintf(stderr, "%s has not been implemented yet\n", __FUNCTION__);
}
