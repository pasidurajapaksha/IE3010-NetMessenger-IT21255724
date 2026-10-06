# NetMessenger

## Multi-Client Chat and File-Sharing Platform over TCP/IP

**Module:** IE3010 - Network Programming  
**Name:** Rajapaksha P. K.  
**Registration Number:** IT21255724  

---

## Personalised Configuration

### Registration Number

```text
IT21255724
```

### Server Port

The last four digits of the registration number are:

```text
5724
```

The server port is calculated as:

```text
6000 + 5724 = 11724
```

Therefore, the server listens on:

```text
TCP Port 11724
```

### Node ID

The numeric part of the registration number is:

```text
21255724
```

Digits 3-6 are:

```text
2557
```

Therefore, the personalised Node ID is:

```text
NID:2557
```

### Personalised Files and Paths

```text
Server source : server_5724.c
Client source : client_5724.c
Makefile      : Makefile_5724
Log file      : netmsg_IT21255724.log
Storage path  : ./storage/IT21255724/<sender_username>/<filename>
```

---

## Implemented Features

NetMessenger implements the required TCP client/server functionality:

- Multiple simultaneous clients
- Unique username registration
- User join and leave notifications
- Connected-user listing
- Broadcast messaging
- Private messaging
- Chat-room creation and joining
- Room listing
- Room messaging
- Leaving chat rooms
- Direct file transfer
- Room file transfer
- Server-side file storage
- Timestamped server logging
- Graceful client disconnect handling
- Unexpected client disconnect handling
- Protocol error handling
- TCP command framing
- Exact-byte binary file transfer

The server uses I/O multiplexing with `select()` to handle multiple connected clients in a single server process.

---

# Build Instructions

The application is written in C using the standard BSD sockets API and can be compiled using the personalised Makefile.

## Build

From the project directory, run:

```bash
make -f Makefile_5724
```

This produces:

```text
server_5724
client_5724
```

## Clean

To remove the compiled executables:

```bash
make -f Makefile_5724 clean
```

---

# Simple User Guide

## 1. Start the Server

Run:

```bash
./server_5724
```

The server listens on the personalised TCP port:

```text
11724
```

---

## 2. Start a Client

Open another terminal and run:

```bash
./client_5724 127.0.0.1 11724
```

Additional clients can be started in separate terminals using the same command.

---

## 3. Register a Username

Registration must be the first protocol command sent after connecting.

Syntax:

```text
REGISTER <username>
```

Example:

```text
REGISTER user1
```

A successful registration returns:

```text
OK REGISTERED user1 NID:2557
```

---

## Application Commands

### List Connected Users

```text
LIST
```

Successful response:

```text
OK USERS <comma-separated-users> NID:2557
```

---

### Broadcast Message

```text
BCAST <message>
```

Example:

```text
BCAST Hello everyone
```

The sender receives:

```text
OK SENT NID:2557
```

Other connected clients receive:

```text
MSG BCAST <sender> <message>
```

---

### Private Message

```text
PMSG <username> <message>
```

Example:

```text
PMSG user2 Hello
```

The sender receives:

```text
OK SENT NID:2557
```

The target receives:

```text
MSG PRIV <sender> <message>
```

---

### Join or Create a Room

```text
JOIN <room>
```

Example:

```text
JOIN testroom
```

Successful response:

```text
OK JOINED testroom NID:2557
```

If the room does not already exist, it is created automatically.

---

### List Rooms

```text
ROOMS
```

Successful response:

```text
OK ROOMS <comma-separated-room-names> NID:2557
```

---

### Send a Room Message

```text
RMSG <room> <message>
```

Example:

```text
RMSG testroom Hello room
```

The sender receives:

```text
OK SENT NID:2557
```

Room members receive:

```text
MSG ROOM <room> <sender> <message>
```

---

### Leave a Room

```text
LEAVE <room>
```

Example:

```text
LEAVE testroom
```

Successful response:

```text
OK LEFT testroom NID:2557
```

---

## File Transfer

The protocol file-transfer command is:

```text
SENDFILE <target> <filename> <filesize>
```

The command header is immediately followed by exactly `<filesize>` raw bytes.

The client provides the following convenient command:

```text
/send <target> <filepath>
```

### Send a File to a User

Example:

```text
/send user2 /tmp/test.bin
```

### Send a File to a Room

Example:

```text
/send testroom /tmp/test.bin
```

A successful transfer returns:

```text
OK FILE_RECEIVED <filename> NID:2557
```

---

## Presence Notifications

When a registered user connects, other clients may receive:

```text
MSG JOIN <username>
```

When a registered user disconnects, other clients may receive:

```text
MSG LEAVE <username>
```

---

## Quit

To disconnect cleanly:

```text
QUIT
```

The server responds:

```text
OK BYE NID:2557
```

and then closes the client connection.

---

# File Storage

The server stores transferred files using the personalised storage structure:

```text
./storage/IT21255724/<sender_username>/<filename>
```

Example:

```text
./storage/IT21255724/user1/test.bin
```

Files received by the client are stored under:

```text
./downloads/<recipient_username>/<sender_username>/<filename>
```

---

# Server Log

Server activity is written to:

```text
netmsg_IT21255724.log
```

The log records timestamped events such as:

- Connections
- Registrations
- Commands
- Messaging activity
- File transfers
- Errors
- Disconnections

---

# Project Files

```text
server_5724.c
client_5724.c
config_5724.h
file_io_5724.h
Makefile_5724
README.md
docs/design-diary.md
docs/prompt-log.txt
docs/reflection.txt
```
