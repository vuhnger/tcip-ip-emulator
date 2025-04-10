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
    if (l4 == NULL || data == NULL || len < 0) {
        fprintf(stderr, "L4SAP_send: Invalid parameters.\n");
        return -1;
    }

    // Truncate message if larger than max payload size
    if (len > L4Payloadsize) {
        fprintf(stderr, "L4SAP_send: Payload is too large, truncating to %d bytes.\n", L4Payloadsize);
        len = L4Payloadsize;
    }

    // Prepare packet with L4 header and data
    uint8_t frame[L4Framesize];
    L4Header *header = (L4Header *)frame;
    
    header->type = L4_DATA;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->expected_recv_seq;
    header->mbz = 0;
    
    // Copy payload after header
    memcpy(frame + sizeof(L4Header), data, len);
    
    // Save a copy of what we're sending for potential retransmissions
    l4->send_state.length = len;
    memcpy(l4->send_state.buffer, data, len);
    
    // Setup for retransmission loop
    int transmission_attempts = 0;
    const int max_attempts = 5;  // 1 initial + 4 retries
    
    struct timeval timeout;
    
    while (transmission_attempts < max_attempts) {
        // Adaptive timeout based on packet size and attempt number
        timeout.tv_sec = (len > 256) ? (1 + (transmission_attempts < 2 ? transmission_attempts : 2)) : 1;
        timeout.tv_usec = 0;
        
        fprintf(stderr, "L4SAP_send: Sending packet with seqno=%d (attempt %d, timeout=%lds)\n", 
                header->seqno, transmission_attempts + 1, timeout.tv_sec);
        
        // Send packet through L2
        int send_result = l2sap_sendto(l4->l2, frame, sizeof(L4Header) + len);
        if (send_result < 0) {
            fprintf(stderr, "L4SAP_send: L2 send failed\n");
            return -1;
        }
        
        // Wait for response - could be ACK, DATA, or RESET
        uint8_t recv_buffer[L4Framesize];
        int recv_result;
        int corrupt_packets = 0;
        const int max_corrupt = 3;  // Maximum consecutive corrupt packets before retransmitting
        
        // Keep receiving until we get an ACK or timeout
        while (corrupt_packets < max_corrupt) {
            recv_result = l2sap_recvfrom_timeout(l4->l2, recv_buffer, L4Framesize, &timeout);
            
            if (recv_result == L2_TIMEOUT) {
                // Timeout waiting for response
                fprintf(stderr, "L4SAP_send: Timeout waiting for ACK\n");
                break;  // Break inner loop to retry transmission
            }
            else if (recv_result < 0) {
                // Negative value indicates an error (possibly checksum)
                corrupt_packets++;
                fprintf(stderr, "L4SAP_send: Ignoring corrupted packet (error code %d, count %d/%d)\n", 
                         recv_result, corrupt_packets, max_corrupt);
                
                if (corrupt_packets >= max_corrupt) {
                    fprintf(stderr, "L4SAP_send: Too many corrupt packets, retransmitting\n");
                    break;  // Break inner loop to retry transmission
                }
                continue;  // Keep waiting for valid packet
            }
            else if (recv_result < sizeof(L4Header)) {
                // Packet too small to contain header
                fprintf(stderr, "L4SAP_send: Received packet too small\n");
                continue;  // Keep waiting for valid packet
            }
            
            // Reset corrupt packet counter since we got a valid packet
            corrupt_packets = 0;
            
            // Process received packet
            L4Header *recv_header = (L4Header *)recv_buffer;
            
            // Check packet type
            if (recv_header->type == L4_RESET) {
                fprintf(stderr, "L4SAP_send: Received RESET packet\n");
                // Do not free resources here, just set terminating flag
                l4->is_terminating = 1;
                return L4_QUIT;
            }
            else if (recv_header->type == L4_ACK) {
                // Got an ACK - check if it's for our current packet
                // In stop-and-wait, an ACK with ackno=N means "I've received up to N-1"
                fprintf(stderr, "L4SAP_send: Received ACK with ackno=%d, current seqno=%d\n", 
                        recv_header->ackno, l4->next_send_seq);
                        
                if (recv_header->ackno == (1 - l4->next_send_seq)) {
                    fprintf(stderr, "L4SAP_send: Got valid ACK (ackno=%d) for our packet with seqno=%d\n", 
                            recv_header->ackno, (1 - recv_header->ackno));
                    
                    // Save last ACK received
                    l4->send_state.last_ack_recieved = recv_header->ackno;
                    
                    // CRITICAL FIX: Update sequence number for next send
                    l4->next_send_seq = 1 - l4->next_send_seq;
                    fprintf(stderr, "L4SAP_send: Updated next_send_seq to %d\n", l4->next_send_seq);
                    
                    // Success! Return number of bytes sent
                    return len;
                }
                else if (recv_header->ackno == l4->next_send_seq) {
                    // This might be an ACK for a packet we're about to send
                    // This can happen in rare cases - log but continue waiting
                    fprintf(stderr, "L4SAP_send: Received ACK for future sequence number, ignoring\n");
                    continue;
                }
                else {
                    fprintf(stderr, "L4SAP_send: Received ACK with unexpected ackno=%d, ignoring\n", 
                            recv_header->ackno);
                    // Keep waiting for correct ACK
                    continue;
                }
            }
            else if (recv_header->type == L4_DATA) {
                // Handle incoming DATA for full-duplex support
                fprintf(stderr, "L4SAP_send: Received DATA packet while waiting for ACK\n");
                
                // Check if it's the sequence number we expect
                if (recv_header->seqno == l4->expected_recv_seq) {
                    fprintf(stderr, "L4SAP_send: DATA has new seqno=%d, sending ACK\n", 
                            recv_header->seqno);
                    
                    // Store the received sequence number
                    l4->recv_state.last_seqno_recieved = recv_header->seqno;
                    
                    // Send ACK for the data
                    uint8_t ack_frame[sizeof(L4Header)];
                    L4Header *ack_header = (L4Header *)ack_frame;
                    ack_header->type = L4_ACK;
                    ack_header->seqno = l4->next_send_seq;
                    ack_header->ackno = (1 - l4->expected_recv_seq);  // Send back an ACK for this seq
                    ack_header->mbz = 0;
                    
                    // Send the ACK
                    int ack_result = l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                    fprintf(stderr, "L4SAP_send: ACK send result: %d\n", ack_result);
                    
                    // Update expected sequence number for next DATA
                    l4->expected_recv_seq = 1 - l4->expected_recv_seq;
                    l4->recv_state.last_ack_sent = recv_header->seqno;
                }
                else {
                    // Duplicate DATA packet, just re-ACK it
                    fprintf(stderr, "L4SAP_send: DATA has duplicate seqno=%d, re-sending ACK\n", 
                            recv_header->seqno);
                    
                    uint8_t ack_frame[sizeof(L4Header)];
                    L4Header *ack_header = (L4Header *)ack_frame;
                    ack_header->type = L4_ACK;
                    ack_header->seqno = l4->next_send_seq;
                    ack_header->ackno = (1 - l4->expected_recv_seq);  // Use the opposite of expected
                    ack_header->mbz = 0;
                    
                    int ack_result = l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                    fprintf(stderr, "L4SAP_send: Duplicate ACK send result: %d\n", ack_result);
                }
                
                // Continue waiting for our ACK
                continue;
            }
        }
        
        // If we get here, timeout occurred or too many corrupt packets - increment attempts and retry
        transmission_attempts++;
    }
    
    // If we get here, we've exceeded max retransmission attempts
    fprintf(stderr, "L4SAP_send: Failed after %d attempts\n", max_attempts);
    return L4_TIMEOUT;  // Return L4_TIMEOUT (which is 0) as per spec
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
    if (l4 == NULL || data == NULL || len <= 0) {
        fprintf(stderr, "L4SAP_recv: Invalid parameters.\n");
        return -1;
    }
    
    fprintf(stderr, "L4SAP_recv: Waiting for data...\n");
    
    uint8_t frame[L4Framesize];
    int copy_len = 0;
    int dup_counter = 0;  // Track consecutive duplicates
    const int max_duplicates = 10;  // Maximum duplicates before assuming connection issue
    int corrupt_counter = 0;  // Track consecutive corrupt packets
    const int max_corrupt = 20;  // Maximum corrupt packets before reporting error
    
    // Keep receiving until we get valid DATA
    while (1) {
        // Receive a packet with no timeout (blocking)
        int recv_result = l2sap_recvfrom_timeout(l4->l2, frame, L4Framesize, NULL);
        
        if (recv_result < 0) {
            // Negative value indicates an error (likely checksum error)
            corrupt_counter++;
            fprintf(stderr, "L4SAP_recv: Ignoring corrupted packet (error code %d, count %d/%d)\n", 
                     recv_result, corrupt_counter, max_corrupt);
                     
            if (corrupt_counter > max_corrupt) {
                // Too many corrupt packets in a row, might indicate serious network issues
                fprintf(stderr, "L4SAP_recv: Too many corrupt packets (%d), possible network failure\n",
                        corrupt_counter);
                return -1;  // Return error after excessive corruption
            }
            
            continue;  // Continue waiting
        }
        
        // Reset corrupt counter since we got a valid packet
        corrupt_counter = 0;
        
        if (recv_result < sizeof(L4Header)) {
            fprintf(stderr, "L4SAP_recv: Received packet too small\n");
            continue;  // Keep waiting for valid packet
        }
        
        // Process received packet
        L4Header *recv_header = (L4Header *)frame;
        
        // Handle different packet types
        if (recv_header->type == L4_RESET) {
            fprintf(stderr, "L4SAP_recv: Received RESET packet\n");
            // Set terminating flag instead of freeing resources directly
            l4->is_terminating = 1;
            return L4_QUIT;
        } 
        else if (recv_header->type == L4_DATA) {
            // Check the sequence number
            if (recv_header->seqno == l4->expected_recv_seq) {
                fprintf(stderr, "L4SAP_recv: Received DATA with expected seqno=%d\n", recv_header->seqno);
                
                // Store the received sequence number
                l4->recv_state.last_seqno_recieved = recv_header->seqno;
                dup_counter = 0;  // Reset duplicate counter
                
                // Copy data to caller's buffer
                copy_len = recv_result - sizeof(L4Header);
                if (copy_len > len) {
                    fprintf(stderr, "L4SAP_recv: Data truncated from %d to %d bytes\n", copy_len, len);
                    copy_len = len;
                }
                memcpy(data, frame + sizeof(L4Header), copy_len);
                
                // Send ACK for the data - possibly repeat for reliability in lossy network
                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - l4->expected_recv_seq);  // ACK with the next expected seqno
                ack_header->mbz = 0;
                
                // Send the ACK multiple times to increase reliability in lossy network
                const int ack_attempts = (copy_len > 256) ? 2 : 1; // Send more ACKs for large packets
                
                for (int i = 0; i < ack_attempts; i++) {
                    fprintf(stderr, "L4SAP_recv: Sending ACK with ackno=%d for seqno=%d (try %d/%d)\n", 
                            ack_header->ackno, recv_header->seqno, i+1, ack_attempts);
                    int ack_result = l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                    fprintf(stderr, "L4SAP_recv: ACK send result: %d\n", ack_result);
                    
                    // Small delay between duplicate ACKs if sending multiple
                    if (i < ack_attempts - 1) {
                        usleep(10000);  // 10ms delay
                    }
                }
                
                // Update expected sequence number for next DATA
                l4->expected_recv_seq = 1 - l4->expected_recv_seq;
                l4->recv_state.last_ack_sent = recv_header->seqno;
                
                // Return with data
                return copy_len;
            } 
            else {
                // Duplicate DATA packet, just re-ACK it and keep waiting
                fprintf(stderr, "L4SAP_recv: Received duplicate DATA with seqno=%d, expected %d\n", 
                        recv_header->seqno, l4->expected_recv_seq);
                
                dup_counter++;
                if (dup_counter > max_duplicates) {
                    fprintf(stderr, "L4SAP_recv: Too many duplicate packets (%d), possible connection problem\n",
                            dup_counter);
                    // Still continue, but log the issue
                }
                
                // Send ACK for the duplicate data
                uint8_t ack_frame[sizeof(L4Header)];
                L4Header *ack_header = (L4Header *)ack_frame;
                ack_header->type = L4_ACK;
                ack_header->seqno = l4->next_send_seq;
                ack_header->ackno = (1 - recv_header->seqno);  // ACK with the opposite of received seqno
                ack_header->mbz = 0;
                
                fprintf(stderr, "L4SAP_recv: Re-sending ACK with ackno=%d for duplicate DATA with seqno=%d\n", 
                        ack_header->ackno, recv_header->seqno);
                int ack_result = l2sap_sendto(l4->l2, ack_frame, sizeof(L4Header));
                fprintf(stderr, "L4SAP_recv: Duplicate ACK send result: %d\n", ack_result);
                
                // Continue waiting for new data
                continue;
            }
        } 
        else if (recv_header->type == L4_ACK) {
            // Process ACKs - could be for our sent data
            fprintf(stderr, "L4SAP_recv: Received ACK with ackno=%d\n", recv_header->ackno);
            
            // For ACKs in stop-and-wait protocol:
            // ackno = 0 means "I received packet with seqno=1, send me seqno=0 next"
            // ackno = 1 means "I received packet with seqno=0, send me seqno=1 next"
            
            // If this ACK is for our current send sequence, update
            if (recv_header->ackno == (1 - l4->next_send_seq)) {
                fprintf(stderr, "L4SAP_recv: ACK confirms our last packet, updating sequence\n");
                l4->send_state.last_ack_recieved = recv_header->ackno;
                // Sequence will be toggled when we actually send next packet
            }
            
            // Continue waiting for DATA packets
            continue;
        }
        else {
            fprintf(stderr, "L4SAP_recv: Received unknown packet type %d\n", recv_header->type);
            continue;
        }
    }
    
    // This should never be reached due to the infinite loop
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
    if (l4 == NULL) {
        return;  // Nothing to do if l4 is NULL
    }
    
    fprintf(stderr, "L4SAP_destroy: Terminating L4 entity\n");
    
    // Only try to send RESET packets if L2 layer exists and we're not already terminating
    if (l4->l2 != NULL && !l4->is_terminating) {
        // Prepare RESET packet
        uint8_t reset_frame[sizeof(L4Header)];
        L4Header *reset_header = (L4Header *)reset_frame;
        
        reset_header->type = L4_RESET;
        reset_header->seqno = l4->next_send_seq;
        reset_header->ackno = l4->expected_recv_seq;
        reset_header->mbz = 0;
        
        // Send multiple RESET packets to ensure peer receives at least one
        const int num_reset_packets = 3;
        
        for (int i = 0; i < num_reset_packets; i++) {
            fprintf(stderr, "L4SAP_destroy: Sending RESET packet %d/%d\n", i+1, num_reset_packets);
            l2sap_sendto(l4->l2, reset_frame, sizeof(L4Header));
            
            // Small delay between packets to avoid overwhelming the network
            usleep(50000);  // 50ms delay
        }
    }
    
    // Clean up resources
    if (l4->l2 != NULL) {
        fprintf(stderr, "L4SAP_destroy: Destroying L2 entity\n");
        l2sap_destroy(l4->l2);
        l4->l2 = NULL;  // Prevent potential double-free
    }
    
    // Free the L4SAP structure itself
    fprintf(stderr, "L4SAP_destroy: Freeing L4 entity memory\n");
    free(l4);
    
    fprintf(stderr, "L4SAP_destroy: Complete\n");
}
