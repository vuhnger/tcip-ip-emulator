# Network Protocol Stack & Maze Solver

A custom network protocol stack implementation in C with a maze-solving application.

## Overview

This project implements a three-layer network protocol stack:

| Layer | Name | Description |
|-------|------|-------------|
| L5 | Application | Maze client that requests, solves, and returns mazes |
| L4 | Transport (L4SAP) | Reliable datagram service using stop-and-wait protocol |
| L2 | Data Link (L2SAP) | UDP-based communication with checksums and framing |

## Features

### L2SAP (Data Link Layer)
- UDP socket-based network communication
- Custom frame format with headers (destination address, length, checksum)
- XOR-based checksum verification
- Timeout-based receive with configurable delays
- Maximum frame size: 1024 bytes

### L4SAP (Transport Layer)
- Reliable datagram delivery over unreliable L2
- Stop-and-wait ARQ protocol with sequence number toggling (0/1)
- ACK-based reliability with automatic retransmission (up to 5 attempts)
- Full-duplex communication support
- Graceful termination via L4_RESET messages

### Maze Application
- Client-server architecture for maze generation and solving
- BFS (Breadth-First Search) algorithm for pathfinding
- ASCII visualization of mazes in terminal
- Seed-based maze generation for reproducibility

## Building

```bash
cd src
cmake . -B build
cmake --build build
```

This produces three executables in `build/`:
- `maze-client` - Main maze application
- `transport-test-client` - L4SAP layer testing
- `datalink-test-client` - L2SAP layer testing

## Usage

### Maze Client
```bash
./build/maze-client <server-ip> <port> <maze-seed>
```
Example:
```bash
./build/maze-client 127.0.0.1 12345 42
```
Connects to a maze server, requests a maze with the given seed, solves it using BFS, and sends the solution back.

### Transport Layer Test
```bash
./build/transport-test-client <server-ip> <port>
```
Runs 20 rounds of send/receive with varying message sizes to test reliable delivery.

### Data Link Layer Test
```bash
./build/datalink-test-client <server-ip> <port>
```
Runs 25 rounds of send/receive to test basic frame transmission and checksums.

## Running with Test Servers

Pre-compiled server binaries are provided in `test-servers/` for multiple platforms:
- `intel-redhat-5.14/` - Red Hat Linux
- `static-intel-ubuntu-24.04/` - Ubuntu 24.04 (static)
- `m3-macos-15.3/` - macOS (Apple Silicon)

Example:
```bash
# Terminal 1: Start the server
./test-servers/m3-macos-15.3/maze-server

# Terminal 2: Run the client
./build/maze-client 127.0.0.1 <port> 1234
```

## Project Structure

```
.
├── src/
│   ├── l2sap.h / l2sap.c        # Data link layer implementation
│   ├── l4sap.h / l4sap.c        # Transport layer implementation
│   ├── maze.h / maze.c          # Maze solving (BFS algorithm)
│   ├── maze-plot.c              # ASCII maze visualization
│   ├── maze-client.c            # Main maze application
│   ├── datalink-test-client.c   # L2SAP test client
│   ├── transport-test-client.c  # L4SAP test client
│   └── CMakeLists.txt           # Build configuration
├── test-servers/                # Pre-compiled server binaries
└── README.txt                   # Original notes and known issues
```

## Protocol Specifications

### L2 Frame Format
| Field | Size | Description |
|-------|------|-------------|
| dst_addr | 4 bytes | Destination IPv4 address |
| len | 2 bytes | Total frame length (network byte order) |
| checksum | 1 byte | XOR checksum of entire frame |
| mbz | 1 byte | Must be zero (padding) |
| payload | variable | Data (max 1016 bytes) |

### L4 Packet Format
| Field | Size | Description |
|-------|------|-------------|
| type | 1 byte | L4_DATA, L4_ACK, or L4_RESET |
| seqno | 1 byte | Sequence number (0 or 1) |
| ackno | 1 byte | Acknowledgment number |
| mbz | 1 byte | Must be zero |
| payload | variable | Data (max 1012 bytes) |

## Requirements

- C11 compatible compiler
- CMake 3.14+
- POSIX-compliant system (Linux, macOS)
- Standard networking libraries (sockets, UDP)

