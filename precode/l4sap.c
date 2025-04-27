#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "l4sap.h"
#include "l2sap.h"

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

    L4Header *header = (L4Header *)l4->send_state.buffer;
    header->type = 0;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->expected_recv_seq;
    header->mbz = 0;

    return l4;
}

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

    const int max_attempts = 5;
    int attempts = 0;
    struct timeval timeout;

    while (attempts < max_attempts)
    {
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

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
            if (rcv->type == L4_RESET)
            {
                l4->is_terminating = 1;
                return L4_QUIT;
            }
            if (rcv->type == L4_ACK)
            {
                if (rcv->ackno == (1 - l4->next_send_seq))
                {
                    l4->send_state.last_ack_recieved = rcv->ackno;
                    l4->next_send_seq = 1 - l4->next_send_seq;
                    return len;
                }
                continue;
            }
            // ignore DATA packets
        }
        attempts++;
    }

    return L4_SEND_FAILED;
}

int l4sap_recv(L4SAP *l4, uint8_t *data, int len)
{
    if (l4 == NULL || data == NULL || len <= 0)
        return -1;

    uint8_t frame[L4Framesize];
    int dup_counter = 0;
    const int max_duplicates = 10;

    while (1)
    {
        int recv_result = l2sap_recvfrom_timeout(l4->l2, frame, L4Framesize, NULL);
        if (recv_result < 0)
        {
            // drop failed packet and continue
            continue;
        }
        if (recv_result < sizeof(L4Header))
            continue;

        L4Header *recv_header = (L4Header *)frame;
        if (recv_header->type == L4_RESET)
        {
            l4->is_terminating = 1;
            return L4_QUIT;
        }
        else if (recv_header->type == L4_DATA)
        {
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

                int ack_attempts = (copy_len > 256) ? 2 : 1;
                for (int i = 0; i < ack_attempts; i++)
                {
                    l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                    if (i < ack_attempts - 1)
                        usleep(1000);
                }

                l4->expected_recv_seq = 1 - l4->expected_recv_seq;
                l4->recv_state.last_ack_sent = recv_header->seqno;
                return copy_len;
            }
            else
            {
                // duplicate packet: resend ACK
                dup_counter++;
                if (dup_counter > max_duplicates)
                {
                    // possible connection problem
                }
                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - recv_header->seqno);
                ack_header->mbz = 0;

                l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                continue;
            }
        }
        else if (recv_header->type == L4_ACK)
        {
            if (recv_header->ackno == (1 - l4->next_send_seq))
                l4->send_state.last_ack_recieved = recv_header->ackno;
            continue;
        }
        else
        {
            continue;
        }
    }
    return -1;
}

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
            usleep(50000);
        }
    }

    if (l4->l2 != NULL)
    {
        l2sap_destroy(l4->l2);
        l4->l2 = NULL;
    }
    free(l4);
}
