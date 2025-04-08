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
L4SAP* l4sap_create(const char* server_ip, int server_port)
{
    // Validate parameters
    if (server_ip == NULL) {
        fprintf(stderr, "L4SAP_create: Invalid server IP\n");
        return NULL;
    }

    // Allocate memory for L4SAP structure
    L4SAP* service_access_point = malloc(sizeof(struct L4SAP));
    if (service_access_point == NULL) {
        fprintf(stderr, "L4SAP_create: Failed to allocate memory for service_access_point\n");
        return NULL;
    }

    // Create L2SAP entity first
    L2SAP* l2 = l2sap_create(server_ip, server_port);
    if (l2 == NULL) {
        fprintf(stderr, "L4SAP_create: Failed to create L2 entity\n");
        free(service_access_point);
        return NULL;
    }

    // Copy the L2SAP structure into the L4SAP
    memcpy(&service_access_point->l2, l2, sizeof(L2SAP));
    
    // Free the original L2SAP since we've copied it
    l2sap_destroy(l2);

    // Initialize sequence numbers and state
    service_access_point->next_send_seq = 0;
    service_access_point->expected_recv_seq = 0;
    service_access_point->is_terminating = 0;
    
    // Initialize send state
    memset(service_access_point->send_state.buffer, 0, L4Payloadsize);
    service_access_point->send_state.length = 0;
    service_access_point->send_state.last_ack_recieved = 0;
    
    // Initialize receive state
    service_access_point->recv_state.last_seqno_recieved = 0;
    service_access_point->recv_state.last_ack_sent = 0;

    printf("L4SAP_create: Successfully created L4 entity\n");
    return service_access_point;
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
int l4sap_send(L4SAP* l4, const uint8_t* data, int len)
{
    // Validate parameters
    if (l4 == NULL || data == NULL || len < 0) {
        fprintf(stderr, "L4SAP_send: Invalid parameters\n");
        return -1;
    }

    // Check if we're already terminating
    if (l4->is_terminating) {
        return L4_QUIT;
    }

    // Truncate message if too large
    if (len > L4Payloadsize) {
        fprintf(stderr, "L4SAP_send: Message truncated to %d bytes\n", L4Payloadsize);
        len = L4Payloadsize;
    }

    // Prepare buffer for sending (header + payload)
    uint8_t packet[sizeof(L4Header) + L4Payloadsize];
    L4Header* header = (L4Header*)packet;
    
    // Set up header
    header->type = L4_DATA;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->recv_state.last_ack_sent;
    header->mbz = 0; // Must be zero
    
    // Copy data to packet
    memcpy(packet + sizeof(L4Header), data, len);
    
    // Save to buffer for potential retransmissions
    memcpy(l4->send_state.buffer, data, len);
    l4->send_state.length = len;
    
    // Set up timeout
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    // Buffer for receiving packets
    uint8_t recv_buffer[sizeof(L4Header) + L4Payloadsize];
    
    // Retransmission loop - try up to 5 times (original + 4 retransmissions)
    for (int attempt = 0; attempt < 5; attempt++) {
        // Send the packet using L2 layer
        int send_result = l2sap_sendto(&l4->l2, packet, sizeof(L4Header) + len);
        if (send_result < 0) {
            fprintf(stderr, "L4SAP_send: Failed to send packet, attempt %d\n", attempt + 1);
            continue; // Try again
        }
        
        // Loop to handle incoming packets while waiting for ACK
        while (1) {
            // Wait for response with timeout
            int recv_result = l2sap_recvfrom_timeout(&l4->l2, recv_buffer, 
                                                   sizeof(recv_buffer), &timeout);
            
            // Check for timeout
            if (recv_result == L2_TIMEOUT) {
                fprintf(stderr, "L4SAP_send: Timeout waiting for response, attempt %d\n", attempt + 1);
                break; // Break inner loop and retry sending
            }
            
            // Check for receive error
            if (recv_result < 0) {
                fprintf(stderr, "L4SAP_send: Error receiving response\n");
                break; // Break inner loop and retry sending
            }
            
            // Ensure we received at least a header
            if (recv_result < sizeof(L4Header)) {
                fprintf(stderr, "L4SAP_send: Received incomplete packet\n");
                continue; // Continue waiting for valid packets
            }
            
            // Parse received packet
            L4Header* recv_header = (L4Header*)recv_buffer;
            
            // Check packet type and handle accordingly
            switch (recv_header->type) {
                case L4_RESET:
                    // Handle RESET - terminate connection
                    fprintf(stderr, "L4SAP_send: Received RESET from peer\n");
                    l4->is_terminating = 1;
                    return L4_QUIT;
                    
                case L4_ACK:
                    // Check if ACK is for our current sequence number
                    if (recv_header->ackno == l4->next_send_seq) {
                        // Update state with received ACK
                        l4->send_state.last_ack_recieved = recv_header->ackno;
                        
                        // Advance sequence number (toggle between 0 and 1 for stop-and-wait)
                        l4->next_send_seq = (l4->next_send_seq + 1) % 2;
                        
                        // Success - return number of bytes sent
                        return len;
                    }
                    fprintf(stderr, "L4SAP_send: Received ACK for wrong sequence number\n");
                    break;
                    
                case L4_DATA:
                    // Handle incoming DATA packet for full-duplex operation
                    
                    // Check if this is a new sequence number we should process
                    if (recv_header->seqno == l4->expected_recv_seq) {
                        // Update received sequence state
                        l4->recv_state.last_seqno_recieved = recv_header->seqno;
                        l4->recv_state.last_ack_sent = recv_header->seqno;
                        
                        // Increment expected sequence number for next packet
                        l4->expected_recv_seq = (l4->expected_recv_seq + 1) % 2;
                    }
                    
                    // Send ACK for the received sequence number
                    uint8_t ack_packet[sizeof(L4Header)];
                    L4Header* ack_header = (L4Header*)ack_packet;
                    ack_header->type = L4_ACK;
                    ack_header->seqno = l4->next_send_seq;
                    ack_header->ackno = l4->recv_state.last_seqno_recieved;
                    ack_header->mbz = 0;
                    
                    l2sap_sendto(&l4->l2, ack_packet, sizeof(L4Header));
                    break;
                    
                default:
                    fprintf(stderr, "L4SAP_send: Received unknown packet type %d\n", 
                            recv_header->type);
                    break;
            }
        }
    }
    
    // All retransmissions failed
    return L4_TIMEOUT;  // Changed from L4_SEND_FAILED to L4_TIMEOUT as specified
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
int l4sap_recv(L4SAP* l4, uint8_t* data, int len)
{
    // Validate parameters
    if (l4 == NULL || data == NULL || len <= 0) {
        fprintf(stderr, "L4SAP_recv: Invalid parameters\n");
        return -1;
    }

    // Check if we're already terminating
    if (l4->is_terminating) {
        return L4_QUIT;
    }

    printf("L4SAP_recv: Waiting for data...\n");
    
    // Buffer for receiving packets (header + payload)
    uint8_t packet[sizeof(L4Header) + L4Payloadsize];
    
    // Loop indefinitely until we get a valid DATA packet or error
    while (1) {
        // Set a reasonable timeout to avoid completely blocking forever
        struct timeval recv_timeout;
        recv_timeout.tv_sec = 5;  // 5 second timeout for diagnostics
        recv_timeout.tv_usec = 0;

        printf("L4SAP_recv: Calling l2sap_recvfrom_timeout...\n");
        
        // Block with timeout
        int recv_result = l2sap_recvfrom_timeout(&l4->l2, packet, sizeof(packet), &recv_timeout);
        
        printf("L4SAP_recv: l2sap_recvfrom_timeout returned %d\n", recv_result);
        
        // Check for timeout - keep trying
        if (recv_result == L2_TIMEOUT) {
            printf("L4SAP_recv: Timeout, continuing to wait...\n");
            continue;
        }
        
        // Check for receive error
        if (recv_result < 0) {
            fprintf(stderr, "L4SAP_recv: Error receiving packet\n");
            return -1;
        }
        
        // Validate packet size (at least header should be present)
        if (recv_result < sizeof(L4Header)) {
            fprintf(stderr, "L4SAP_recv: Received incomplete packet (%d bytes)\n", recv_result);
            continue;
        }
        
        // Process the received packet header
        L4Header* header = (L4Header*)packet;
        
        printf("L4SAP_recv: Received packet type=%d, seqno=%d, ackno=%d\n", 
               header->type, header->seqno, header->ackno);
        
        // Handle different packet types
        switch (header->type) {
            case L4_RESET:
                // Process RESET - terminate connection
                fprintf(stderr, "L4SAP_recv: Received RESET from peer\n");
                l4->is_terminating = 1;
                return L4_QUIT;
                
            case L4_ACK:
                // Process ACK - update state
                printf("L4SAP_recv: Processing ACK, ackno=%d\n", header->ackno);
                l4->send_state.last_ack_recieved = header->ackno;
                // Continue waiting for data
                break;
                
            case L4_DATA:
                printf("L4SAP_recv: Processing DATA, seqno=%d, expected=%d\n", 
                       header->seqno, l4->expected_recv_seq);
                
                // Check the sequence number
                if (header->seqno == l4->expected_recv_seq) {
                    // This is a new packet with expected sequence number
                    printf("L4SAP_recv: Valid new DATA packet received\n");
                    
                    // Calculate payload size
                    int payload_size = recv_result - sizeof(L4Header);
                    
                    // Copy data to user buffer (truncate if needed)
                    int copy_size = (payload_size <= len) ? payload_size : len;
                    memcpy(data, packet + sizeof(L4Header), copy_size);
                    
                    // Update state
                    l4->recv_state.last_seqno_recieved = header->seqno;
                    l4->recv_state.last_ack_sent = header->seqno;
                    
                    // Advance expected sequence number for next packet
                    l4->expected_recv_seq = (l4->expected_recv_seq + 1) % 2;
                    
                    // Send ACK
                    uint8_t ack_packet[sizeof(L4Header)];
                    L4Header* ack_header = (L4Header*)ack_packet;
                    ack_header->type = L4_ACK;
                    ack_header->seqno = l4->next_send_seq;
                    ack_header->ackno = l4->recv_state.last_seqno_recieved;
                    ack_header->mbz = 0;
                    
                    printf("L4SAP_recv: Sending ACK for seqno=%d\n", 
                           l4->recv_state.last_seqno_recieved);
                    
                    l2sap_sendto(&l4->l2, ack_packet, sizeof(L4Header));
                    
                    // Return the number of bytes copied
                    return copy_size;
                } 
                else {
                    // This is a retransmission of a packet we've already processed
                    printf("L4SAP_recv: Duplicate DATA packet, sending ACK again\n");
                    
                    // Send ACK again but don't return - continue waiting for new data
                    uint8_t ack_packet[sizeof(L4Header)];
                    L4Header* ack_header = (L4Header*)ack_packet;
                    ack_header->type = L4_ACK;
                    ack_header->seqno = l4->next_send_seq;
                    ack_header->ackno = l4->recv_state.last_seqno_recieved;
                    ack_header->mbz = 0;
                    
                    l2sap_sendto(&l4->l2, ack_packet, sizeof(L4Header));
                }
                break;
                
            default:
                fprintf(stderr, "L4SAP_recv: Received unknown packet type %d\n", header->type);
                break;
        }
    }
    
    // We should never reach here because the loop is infinite
    return -1;
}

/** This function is called to terminate the L4 entity and
 *  free all of its resources.
 *  We recommend that you send several L4_RESET packets from
 *  this function to ensure that the peer entity is also
 *  terminating correctly.
 */
void l4sap_destroy(L4SAP* l4)
{
    // Check if pointer is valid
    if (l4 == NULL) {
        return;
    }
    
    // Mark as terminating
    l4->is_terminating = 1;
    
    // Create a RESET packet
    uint8_t reset_packet[sizeof(L4Header)];
    L4Header* header = (L4Header*)reset_packet;
    header->type = L4_RESET;
    header->seqno = l4->next_send_seq;
    header->ackno = l4->recv_state.last_ack_sent;
    header->mbz = 0; // Must be zero
    
    // Send the RESET packet multiple times to increase reliability
    for (int i = 0; i < 3; i++) {
        l2sap_sendto(&l4->l2, reset_packet, sizeof(L4Header));
        
        // Small delay between retransmissions (100ms)
        usleep(100000);  // 100,000 microseconds = 100ms
    }
    
    // Clean up resources
    //l2sap_destroy(&l4->l2);
    free(l4);
}

