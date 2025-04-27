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
    fprintf(stderr, "%s: called with server_ip=%s, port=%d\n",
            __FUNCTION__, server_ip, server_port);

    if (server_ip == NULL || server_port <= 0)
    {
        fprintf(stderr, "%s: invalid parameters.\n", __FUNCTION__);
        return NULL;
    }

    fprintf(stderr, "%s allocating L4SAP structure\n", __FUNCTION__);
    L4SAP *l4 = malloc(sizeof(L4SAP));
    if (!l4)
    {
        fprintf(stderr, "%s, failed to allocate memory for L4SAP.\n", __FUNCTION__);
        return NULL;
    }

    fprintf(stderr, "%s, Creating L2SAP instance\n", __FUNCTION__);
    l4->l2 = l2sap_create(server_ip, server_port);
    if (l4->l2 == NULL)
    {
        fprintf(stderr, "%s failed to create L2SAP.\n", __FUNCTION__);
        l2sap_destroy(l4->l2);
        free(l4);
        return NULL;
    }

    fprintf(stderr, "%s, initializing L4SAP fields\n", __FUNCTION__);
    l4->next_send_seq = 0;
    l4->expected_recv_seq = 0;
    l4->is_terminating = 0;
    l4->send_state.length = 0;
    l4->send_state.last_ack_recieved = 0;
    l4->recv_state.last_seqno_recieved = 0;
    l4->recv_state.last_ack_sent = 0;

    fprintf(stderr, "%s Initializing send_state buffer\n", __FUNCTION__);
    fprintf(stderr, "%s: size of buffer = %zu\n", __FUNCTION__, sizeof(l4->send_state.buffer));

    memset(l4->send_state.buffer, 0, sizeof(l4->send_state.buffer));

    fprintf(stderr, "%s setting up L4 Header\n", __FUNCTION__);
    L4Header *header = (L4Header *)l4->send_state.buffer;
    header->type = 0;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->expected_recv_seq;
    header->mbz = 0;

    fprintf(stderr, "%s completed successfully\n", __FUNCTION__);
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
        fprintf(stderr, "%s: invalid parameters\n", __FUNCTION__);
        return -1;
    }

    if (len > L4Payloadsize)
    {
        fprintf(stderr, "%s: payload too large, truncating to %d bytes\n",
                __FUNCTION__, L4Payloadsize);
        len = L4Payloadsize;
    }

    uint8_t frame[L4Framesize];
    L4Header *header = (L4Header *)frame;

    header->type = L4_DATA;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->expected_recv_seq;
    header->mbz = 0;

    memcpy(frame + sizeof(L4Header), data, len);

    l4->send_state.length = len;
    memcpy(l4->send_state.buffer, data, len);

    const int max_attempts = 5;
    int attempts = 0;
    struct timeval timeout;

    while (attempts < max_attempts)
    {
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        fprintf(stderr, "%s: sending seqno=%d (attempt %d/%d)\n",
                __FUNCTION__, header->seqno, attempts + 1, max_attempts);

        int send_res = l2sap_sendto(l4->l2, frame,
                                    sizeof(L4Header) + len);
        if (send_res < 0)
        {
            fprintf(stderr, "%s: L2 send failed on attempt %d\n",
                    __FUNCTION__, attempts + 1);
            attempts++;
            continue;
        }

        uint8_t recv_buf[L4Framesize];
        int corrupt_count = 0;
        const int max_corrupt = 3;
        int recv_res;

        while (1)
        {
            recv_res = l2sap_recvfrom_timeout(l4->l2,recv_buf,L4Framesize,&timeout);

            if (recv_res == L2_TIMEOUT)
            {
                fprintf(stderr, "%s: timeout waiting for ACK\n",
                        __FUNCTION__);
                break;
            }
            if (recv_res < 0)
            {
                if (++corrupt_count >= max_corrupt)
                {
                    fprintf(stderr, "%s: too many corrupt pkts, retrying\n",
                            __FUNCTION__);
                    break;
                }
                continue;
            }
            if (recv_res < sizeof(L4Header))
            {
                continue;
            }

            L4Header *rcv = (L4Header *)recv_buf;

            if (rcv->type == L4_RESET)
            {
                fprintf(stderr, "%s: received RESET\n", __FUNCTION__);
                l4->is_terminating = 1;
                return L4_QUIT;
            }
            if (rcv->type == L4_ACK)
            {
                if (rcv->ackno == (1 - l4->next_send_seq))
                {
                    l4->send_state.last_ack_recieved = rcv->ackno;
                    l4->next_send_seq = 1 - l4->next_send_seq;
                    fprintf(stderr, "%s: ACK ok, next_send_seq=%d\n",
                            __FUNCTION__, l4->next_send_seq);
                    return len;
                }
                fprintf(stderr, "%s: ignore ACK ackno=%d\n",
                        __FUNCTION__, rcv->ackno);
                continue;
            }
            if (rcv->type == L4_DATA)
            {
                continue;
            }
        }

        attempts++;
    }

    fprintf(stderr, "%s: failed after %d attempts\n",
            __FUNCTION__, max_attempts);
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
    {
        fprintf(stderr, "%s: Invalid parameters.\n", __FUNCTION__);
        return -1;
    }

    fprintf(stderr, "%s: Waiting for data...\n", __FUNCTION__);

    uint8_t frame[L4Framesize];
    int copy_len = 0;
    int dup_counter = 0;
    const int max_duplicates = 10;
    int corrupt_counter = 0;
    const int max_corrupt = 20;

    while (1)
    {
        int recv_result = l2sap_recvfrom_timeout(l4->l2, frame, L4Framesize, NULL);

        if (recv_result < 0)
        {
            corrupt_counter++;
            fprintf(stderr, "%s: Ignoring corrupted packet (error  -1, count %d/%d)\n",
                    __FUNCTION__, corrupt_counter, max_corrupt);

            if (corrupt_counter > max_corrupt)
            {
                fprintf(stderr, "%s: too many corrupt packets (%d)\n",
                        __FUNCTION__, corrupt_counter);
                return -1;
            }

            continue;
        }

        corrupt_counter = 0;

        if (recv_result < sizeof(L4Header))
        {
            fprintf(stderr, "%s: Received packet too small\n",
                    __FUNCTION__);
            continue;
        }

        L4Header *recv_header = (L4Header *)frame;

        if (recv_header->type == L4_RESET)
        {
            fprintf(stderr, "%s: Received RESET packet\n", __FUNCTION__);

            l4->is_terminating = 1;
            return L4_QUIT;
        }
        else if (recv_header->type == L4_DATA)
        {

            if (recv_header->seqno == l4->expected_recv_seq)
            {
                fprintf(stderr, "%s: Received DATA with expected seqno=%d\n", __FUNCTION__, recv_header->seqno);

                l4->recv_state.last_seqno_recieved = recv_header->seqno;
                dup_counter = 0;

                copy_len = recv_result - sizeof(L4Header);
                if (copy_len > len)
                {
                    fprintf(stderr, "%s: Data truncated from %d to %d bytes\n", __FUNCTION__, copy_len, len);
                    copy_len = len;
                }
                memcpy(data, frame + sizeof(L4Header), copy_len);

                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - l4->expected_recv_seq);
                ack_header->mbz = 0;

                const int ack_attempts = (copy_len > 256) ? 2 : 1;

                for (int i = 0; i < ack_attempts; i++)
                {
                    fprintf(stderr, "%s: Sending ACK with ackno=%d for seqno=%d (try %d/%d)\n", __FUNCTION__,
                            ack_header->ackno, recv_header->seqno, i + 1, ack_attempts);
                    int ack_result = l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                    fprintf(stderr, "%s: ACK send result: %d\n", __FUNCTION__, ack_result);

                    if (i < ack_attempts - 1)
                    {
                        usleep(1000); // ???
                    }
                }

                l4->expected_recv_seq = 1 - l4->expected_recv_seq;
                l4->recv_state.last_ack_sent = recv_header->seqno;

                return copy_len;
            }
            else
            {

                fprintf(stderr, "%s: Received duplicate DATA with seqno=%d, expected %d\n", __FUNCTION__,
                        recv_header->seqno, l4->expected_recv_seq);

                dup_counter++;
                if (dup_counter > max_duplicates)
                {
                    fprintf(stderr, "%s: Too many duplicate packets (%d), possible connection problem\n", __FUNCTION__,
                            dup_counter);
                }

                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - recv_header->seqno);
                ack_header->mbz = 0;

                fprintf(stderr, "%s: Re-sending ACK with ackno=%d for duplicate DATA with seqno=%d\n", __FUNCTION__,
                        ack_header->ackno, recv_header->seqno);
                int ack_result = l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                fprintf(stderr, "%s: Duplicate ACK send result: %d\n", __FUNCTION__, ack_result);

                continue;
            }
        }
        else if (recv_header->type == L4_ACK)
        {

            fprintf(stderr, "%s: Received ACK with ackno=%d\n", __FUNCTION__, recv_header->ackno);

            if (recv_header->ackno == (1 - l4->next_send_seq))
            {
                fprintf(stderr, "%s: ACK confirms our last packet, updating sequence\n", __FUNCTION__);
                l4->send_state.last_ack_recieved = recv_header->ackno;
            }

            continue;
        }
        else
        {
            fprintf(stderr, "%s: Received unknown packet type %d\n", __FUNCTION__, recv_header->type);
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
    {
        return;
    }

    fprintf(stderr, "%s: Terminating L4 entity\n", __FUNCTION__);

    if (l4->l2 != NULL && !l4->is_terminating)
    {

        uint8_t reset_frame[sizeof(L4Header)];
        L4Header *reset_header = (L4Header *)reset_frame;

        reset_header->type = L4_RESET;
        reset_header->seqno = l4->next_send_seq;
        reset_header->ackno = l4->expected_recv_seq;
        reset_header->mbz = 0;

        const int num_reset_packets = 3;

        for (int i = 0; i < num_reset_packets; i++)
        {
            fprintf(stderr, "%s: Sending RESET packet %d/%d\n", __FUNCTION__, i + 1, num_reset_packets);
            l2sap_sendto(l4->l2, reset_frame, sizeof(L4Header));

            usleep(50000); // ??
        }
    }

    if (l4->l2 != NULL)
    {
        fprintf(stderr, "%s: Destroying L2 entity\n", __FUNCTION__);
        l2sap_destroy(l4->l2);
        l4->l2 = NULL;
    }

    fprintf(stderr, "%s: Freeing L4 entity memory\n", __FUNCTION__);
    free(l4);

    fprintf(stderr, "%s: Complete\n", __FUNCTION__);
}
