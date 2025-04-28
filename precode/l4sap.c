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
    if (server_ip == NULL || server_port <= 0)
        return NULL;

    L4SAP *l4 = malloc(sizeof(L4SAP));
    if (!l4)
        return NULL;

    l4->l2 = l2sap_create(server_ip, server_port);
    if (l4->l2 == NULL)
    {
        free(l4);
        return NULL;
    }

    l4->next_send_seq = 0;
    l4->expected_recv_seq = 0;
    l4->is_terminating = 0;
    l4->send_state.length = 0;
    l4->send_state.last_ack_recieved = 0;
    l4->recv_state.last_seqno_recieved = 0;
    l4->recv_state.last_ack_sent = 0;

    memset(l4->send_state.buffer, 0, sizeof(l4->send_state.buffer));
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
        return -1;

    if (len > L4Payloadsize)
        len = L4Payloadsize;

    uint8_t frame[L4Framesize];
    L4Header *header = (L4Header *)frame;
    header->type = L4_DATA;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->expected_recv_seq;
    header->mbz = 0;

    memcpy(frame + sizeof(L4Header), data, len);
    l4->send_state.length = len;
    memcpy(l4->send_state.buffer, data, len);

    // 1 transmission + 4 retries according to assignment
    const int max_attempts = 5;
    int attempts = 0;
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    while (attempts < max_attempts)
    {
        int send_res = l2sap_sendto(l4->l2, frame, sizeof(L4Header) + len);
        if (send_res < 0)
        {
            attempts++;
            continue;
        }

        uint8_t recv_buf[L4Framesize];
        int recv_res;

        while (1)
        {
            recv_res = l2sap_recvfrom_timeout(l4->l2, recv_buf, L4Framesize, &timeout);
            if (recv_res == L2_TIMEOUT)
                break;
            if (recv_res < sizeof(L4Header))
                continue;

            L4Header *rcv = (L4Header *)recv_buf;

            switch (rcv->type)
            {
                // expecting caller to free L4 when L4_QUIT is returned as in transport-test-client
            case L4_RESET:
                l4->is_terminating = 1;
                return L4_QUIT;

            case L4_ACK:
                if (rcv->ackno == (1 - l4->next_send_seq))
                {
                    l4->send_state.last_ack_recieved = rcv->ackno;
                    l4->next_send_seq = 1 - l4->next_send_seq;
                    return L4_ACK_RECEIVED;
                }
                continue;

            case L4_DATA:
            {
                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - rcv->seqno);
                ack_header->mbz = 0;

                l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                fprintf(stderr, "%s: sending ack for data\n", __FUNCTION__);
                continue;
            }
            default:
                fprintf(stderr, "%s: unknown / uninitalized packet type %d\n", __FUNCTION__, rcv->type);
                continue;
            }
        }
        attempts++;
    }

    return L4_SEND_FAILED;
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
    if (l4 == NULL || data == NULL || len <= 0)
        return -1;

    uint8_t frame[L4Framesize];

    while (1)
    {
        int recv_result = l2sap_recvfrom_timeout(l4->l2, frame, L4Framesize, NULL);
        if (recv_result < 0)
            continue;
        if (recv_result < sizeof(L4Header))
            continue;

        L4Header *recv_header = (L4Header *)frame;

        switch (recv_header->type)
        {
        case L4_RESET:
            l4->is_terminating = 1;
            return L4_QUIT;

        case L4_DATA:
            if (recv_header->seqno == l4->expected_recv_seq)
            {
                int copy_len = recv_result - sizeof(L4Header);
                if (copy_len > len)
                    copy_len = len;
                memcpy(data, frame + sizeof(L4Header), copy_len);

                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - l4->expected_recv_seq);
                ack_header->mbz = 0;

                l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));

                l4->expected_recv_seq = 1 - l4->expected_recv_seq;
                l4->recv_state.last_ack_sent = recv_header->seqno;
                return copy_len;
            }
            else
            {
                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - recv_header->seqno);
                ack_header->mbz = 0;

                l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                continue;
            }

        case L4_ACK:
            if (recv_header->ackno == (1 - l4->next_send_seq))
                l4->send_state.last_ack_recieved = recv_header->ackno;
            continue;

        default:
            fprintf(stderr, "%s: unknown / uninitalized packet type %d\n", __FUNCTION__, recv_header->type);
            continue;
        }
    }

    return -1;
}

/** This function is called to terminate the L4 entity and
 *  free all of its resources.
 *  We recommend that you send several L4_RESET packets from
 *  this function to ensure that the peer entity is also
 *  terminating correctly.
 */
void l4sap_destroy(L4SAP *l4)
{
    if (l4 == NULL)
        return;

    if (l4->l2 != NULL && !l4->is_terminating)
    {
        uint8_t reset_frame[sizeof(L4Header)];
        L4Header *reset_header = (L4Header *)reset_frame;
        reset_header->type = L4_RESET;
        reset_header->seqno = l4->next_send_seq;
        reset_header->ackno = l4->expected_recv_seq;
        reset_header->mbz = 0;

        for (int i = 0; i < 3; i++)
        {
            l2sap_sendto(l4->l2, reset_frame, sizeof(L4Header));
        }
    }

    if (l4->l2 != NULL)
    {
        l2sap_destroy(l4->l2);
        l4->l2 = NULL;
    }
    free(l4);
}
