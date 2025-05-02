# IN2140 Home Exam Spring 2025

## Issue: valgrind errors on datalink-test-client, transport-test-client

When running our client with valgrind, we get warnings about uninitialized memory.
The main issue seems to happen in l4sap_send, where Valgrind reports that a conditional jump depends on uninitialized values.
According to the error messages, the uninitialized value comes from a stack allocation,
and the problem shows up when we copy data into buffers and later use them in conditions or send them over the network.
We believe this happens because the len value we pass in is sometimes much larger than the actual data we put into the buffer.
Even though we tried using memset to zero out the buffers, the warnings didn’t go away.
We think this is because the buffer still contains uninitialized bytes when we copy or send more than what was actually written.


We added the following LOC to datalink-test-client.c to remove valgrind errors in datalink-test-client:

memset(buffer, 0, sizeof(buffer));
 

valgrind error:
--79695-- REDIR: 0x49068c0 (libc.so.6:free) redirected to 0x4847ada (free)
==79695== 
==79695== HEAP SUMMARY:
==79695==     in use at exit: 0 bytes in 0 blocks
==79695==   total heap usage: 2 allocs, 2 frees, 1,060 bytes allocated
==79695== 
==79695== All heap blocks were freed -- no leaks are possible
==79695== 
==79695== ERROR SUMMARY: 4 errors from 2 contexts (suppressed: 0 from 0)
==79695== 
==79695== 2 errors in context 1 of 2:
==79695== Conditional jump or move depends on uninitialised value(s)
==79695==    at 0x40190C: l4sap_send (l4sap.c:121)
==79695==    by 0x401410: main (transport-test-client.c:46)
==79695==  Uninitialised value was created by a stack allocation
==79695==    at 0x401704: l4sap_send (l4sap.c:66)
==79695== 
==79695== 
==79695== 2 errors in context 2 of 2:
==79695== Conditional jump or move depends on uninitialised value(s)
==79695==    at 0x4018B4: l4sap_send (l4sap.c:113)
==79695==    by 0x401410: main (transport-test-client.c:46)
==79695==  Uninitialised value was created by a stack allocation
==79695==    at 0x401704: l4sap_send (l4sap.c:66)
==79695== 
==79695== ERROR SUMMARY: 4 errors from 2 contexts (suppressed: 0 from 0)

## Assignment text: l4sap.c and l4sap.h: ambigiuty regarding return values

There is a contradiction between the code and header file regarding the
return value when all retransmission attempts fail.
The code file states that the function returns L4_SEND_FAILED after 4 failed retransmissions,
while the header file says it gives up after 5 attempts and returns L4_TIMEOUT.
This inconsistency is confusing as it is unclear what the correct return value should be in the failure case.
We have contacted IN2140 staff regarding the issue, but recieved no definitive answer.

These paragraphs contradict regarding which error code to return:

l4sap.c:
 * Waiting for a correct ACK may fail after a timeout of 1 second
 * (timeval.tv_sec = 1, timeval.tv_usec = 0). The function retransmits
 * the packet in that case.
 * The function attempts up to 4 retransmissions. If the last retransmission
 * fails with a timeout as well, the function returns L4_SEND_FAILED.

l4sap.h:
 * l4sap_send resends up to 5 times after a timeout of 1
 * second if it does not receive a correct ACK. After that, it
 * gives up and returns L4_TIMEOUT as an error code.

## Issue: Defining "size" field in L2Header

Note from students:

We defined the L2Header‘s size field as the length of the
payload plus the size of the header itself in bytes.
We’re not sure if this is semantically correct,
but it’s the value the server accepts, so we kept it this way.
This could cause confusion if the intended meaning was just the payload length,
so clarification would be helpful.

We believe that the correct defition of size is the payload length. 

## Issue: memory leaks in datalink-test-server when incorrectly exiting program

When we terminate datalink-test-server using Ctrl+C, it leads to a
memory leak because cleanup code (like freeing allocated memory and deleting entities)
isn’t executed. This is expected since the signal interrupts
normal flow, but it’s worth mentioning as it shows up in memory analysis tools.
Proper signal handling could prevent this, but was outside our scope.

valgrind report:
==2133446==
==2133446== HEAP SUMMARY:
==2133446==     in use at exit: 390,166 bytes in 406 blocks
==2133446==   total heap usage: 822 allocs, 416 frees, 486,012 bytes allocated
==2133446==
==2133446== 961 bytes in 1 blocks are definitely lost in loss record 1 of 2
==2133446==    at 0x4846743: operator new[](unsigned long) (vg_replace_malloc.c:729)
==2133446==    by 0x404D7F: Grid::Grid(int) (wilsons.cc:91)
==2133446==    by 0x4047CD: makeMaze (wilsons.cc:223)
==2133446==    by 0x4026F3: process_message (maze-server.c:108)
==2133446==    by 0x40257B: main (maze-server.c:64)
==2133446==
==2133446== 389,205 bytes in 405 blocks are definitely lost in loss record 2 of 2
==2133446==    at 0x4846743: operator new[](unsigned long) (vg_replace_malloc.c:729)
==2133446==    by 0x404D7F: Grid::Grid(int) (wilsons.cc:91)
==2133446==    by 0x40484B: makeMaze (wilsons.cc:232)
==2133446==    by 0x4026F3: process_message (maze-server.c:108)
==2133446==    by 0x40257B: main (maze-server.c:64)
==2133446==
==2133446== LEAK SUMMARY:
==2133446==    definitely lost: 390,166 bytes in 406 blocks
==2133446==    indirectly lost: 0 bytes in 0 blocks
==2133446==      possibly lost: 0 bytes in 0 blocks
==2133446==    still reachable: 0 bytes in 0 blocks
==2133446==         suppressed: 0 bytes in 0 blocks
==2133446==
==2133446== For lists of detected and suppressed errors, rerun with: -s
==2133446== ERROR SUMMARY: 2 errors from 2 contexts (suppressed: 0 from 0)

## Issue: checksum errors on L2 and L4 communication between client and server

We’ve saw checksum mismatches between the server and client.
We believe this stems from the earlier assumption about the definition or calculation of the packet size.
If the client and server interpret the size of the data differently—especially regarding whether headers
are included or excluded—this can lead to inconsistent data being hashed or verified, resulting in checksum failures.

