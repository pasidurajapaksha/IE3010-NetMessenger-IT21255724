# NetMessenger
### Multi-Client Chat and File Sharing over TCP/IP

## Overview

NetMessenger is a client-server application developed for the **IE3010 — Network Programming** assignment. It will support simultaneous users, messaging, chat rooms, and file sharing through a central TCP server.

The application will be implemented in **C**, using the **BSD sockets API**, and compiled with **GCC on Linux**.

## Student Information

**Registration Number:** IT21255724  
**Module:** IE3010 — Network Programming

## Personalised Configuration

### Server Port

The last four digits of the registration number are **5724**.

**Port = 6000 + 5724 = 11724**

### Node ID

The numeric part of the registration number is **21255724**. Digits 3–6 are **2557**.

**Node ID: NID:2557**

### Project Filenames

- **Server:** server_5724.c
- **Client:** client_5724.c
- **Makefile:** Makefile_5724
- **Server log:** netmsg_IT21255724.log
- **Submission archive:** IE3010_IT21255724.zip

### Server Storage

Received files will be stored under **./storage/IT21255724/**, inside a folder named after the sender, using the received filename.

For example, a file named report.pdf sent by amal will be stored at:

**./storage/IT21255724/amal/report.pdf**

## Planned Features

- Support at least five simultaneous clients.
- Register unique usernames and announce when users join or leave.
- List currently connected users.
- Send broadcast and private messages.
- Create, join, leave, and list chat rooms.
- Deliver room messages only to room members.
- Transfer files to users and rooms through the server.
- Handle malformed commands and unexpected disconnects.
- Log connection, messaging, and file-transfer events with timestamps.

## Protocol

The application will follow the assignment’s specified TCP protocol.

Text commands and responses end with a newline. Every **OK** and **ERR** response ends with **NID:2557**. Forwarded **MSG** lines do not include this tag.

Each **SENDFILE** command is followed immediately by exactly the declared number of raw file bytes, with no additional newline before or after the payload.

## Development Environment

- **Operating system:** Linux
- **Programming language:** C
- **Networking API:** BSD sockets
- **Compiler:** GCC
- **Build tool:** GNU Make
- **Version control:** Git and GitHub

## Build and Execution

Build and run instructions will be added when the initial server, client, and Makefile are implemented and tested.

## Documentation

Development records will include:

- **Design diary:** decisions, challenges, and solutions.
- **AI assistance log:** substantive prompts, evaluation, and changes made.
- **Implementation report:** architecture, implementation explanations, and genuine screenshots.
- **Testing summary:** test cases, expected outcomes, and actual results.
- **Reflection:** learning and evaluation of AI assistance.

## Current Status

Initial repository setup and documentation. The new application implementation has not yet been added.
