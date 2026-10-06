# NetMessenger

## Multi-Client Chat and File-Sharing Platform over TCP/IP

NetMessenger is a TCP client-server application developed in C for the **IE3010 — Network Programming** assignment.

The application uses the BSD sockets API and supports multiple simultaneous clients, messaging, chat rooms, file sharing, presence notifications, graceful disconnect handling, error handling, and timestamped server-side logging.

---

## Student Information

**Registration Number:** IT21255724  
**Module:** IE3010 — Network Programming

---

## Personalised Configuration

### Server Port

The last four digits of the registration number are:

```text
5724
```

The server port is calculated as:

```text
6000 + 5724 = 11724
```

**Server Port:** `11724`

### Node ID

The numeric part of the registration number is:

```text
21255724
```

Digits 3–6 are:

```text
2557
```

Therefore:

**Node ID:** `NID:2557`

### Personalised File Names and Paths

| Item | Personalised Value |
|---|---|
| Registration Number | `IT21255724` |
| Server Port | `11724` |
| Server Source | `server_5724.c` |
| Client Source | `client_5724.c` |
| Makefile | `Makefile_5724` |
| Node ID | `NID:2557` |
| Log File | `netmsg_IT21255724.log` |
| File Storage Path | `./storage/IT21255724/<sender_username>/<filename>` |
| Submission Archive | `IE3010_IT21255724.zip` |

Additional source headers used by the project:

- `config_5724.h`
- `file_io_5724.h`

---

## Implemented Features

NetMessenger supports:

- User registration with unique usernames
- Listing connected users
- Broadcast messaging
- Private messaging
- Join and leave presence notifications
- Chat room creation and membership
- Room listing
- Room messaging
- Direct file transfer
- Room file transfer
- Binary file transfer over TCP
- Graceful client disconnect using `QUIT`
- Unexpected client disconnect cleanup
- Timestamped server logging
- Personalised server-side file storage
- Multiple simultaneous clients

The server uses a `select()`-based concurrency model and supports at least five simultaneous clients.

---

# Build Instructions

The project is designed to compile using GCC on Linux.

## Clean Previous Build

```bash
make -f Makefile_5724 clean
```

## Build Server and Client

```bash
make -f Makefile_5724
```

This creates:

```text
server_5724
client_5724
```

---

# Simple User Guide

## 1. Start the Server

Open a terminal in the project directory and run:

```bash
./server_5724
```

The server listens on the personalised TCP port:

```text
11724
```

Keep this terminal running while clients connect.

---

## 2. Start a Client

Open another terminal and run:

```bash
./client_5724 127.0.0.1
```

Use `127.0.0.1` when the server and client are running on the same computer.

If the client is running on another computer, replace `127.0.0.1` with the IPv4 address of the server.

Example:

```bash
./client_5724 192.168.1.10
```

Multiple client terminals can be opened at the same time.

---

## 3. Register a Username

Registration must be completed before using the other application commands.

Example:

```text
REGISTER Pasidu
```

Successful response:

```text
Server: OK REGISTERED Pasidu NID:2557
```

Each connected client must use a unique username.

If the username is already in use:

```text
Server: ERR 001 USERNAME_TAKEN NID:2557
```

---

# Application Commands

| Command | Description | Example |
|---|---|---|
| `REGISTER <username>` | Register a unique username | `REGISTER Pasidu` |
| `LIST` | List connected registered users | `LIST` |
| `BCAST <message>` | Send a message to all other registered users | `BCAST Hello everyone` |
| `PMSG <username> <message>` | Send a private message to one user | `PMSG Amal Hello Amal` |
| `JOIN <room>` | Create or join a room | `JOIN study` |
| `LEAVE <room>` | Leave a room | `LEAVE study` |
| `ROOMS` | List active rooms | `ROOMS` |
| `RMSG <room> <message>` | Send a message to room members | `RMSG study Hello team` |
| `/send <target> <path>` | Send a file to a user or room | `/send Amal /tmp/test.txt` |
| `QUIT` | Disconnect cleanly from the server | `QUIT` |

---

## 4. List Connected Users

Enter:

```text
LIST
```

Example response:

```text
OK USERS Amal,Nimal,Pasidu NID:2557
```

---

## 5. Send a Broadcast Message

Enter:

```text
BCAST Hello everyone
```

The sender receives:

```text
OK SENT NID:2557
```

Other registered clients receive:

```text
MSG BCAST Pasidu Hello everyone
```

---

## 6. Send a Private Message

Enter:

```text
PMSG Amal Hello Amal
```

The sender receives:

```text
OK SENT NID:2557
```

Amal receives:

```text
MSG PRIV Pasidu Hello Amal
```

If the target user does not exist:

```text
ERR 002 USER_NOT_FOUND NID:2557
```

---

## 7. Create or Join a Room

Enter:

```text
JOIN study
```

Successful response:

```text
OK JOINED study NID:2557
```

If the room does not already exist, the server creates it automatically.

Other users can join the same room using the same command:

```text
JOIN study
```

---

## 8. List Rooms

Enter:

```text
ROOMS
```

Example response:

```text
OK ROOMS study NID:2557
```

---

## 9. Send a Room Message

First join the room:

```text
JOIN study
```

Then send a message:

```text
RMSG study Hello everyone
```

The sender receives:

```text
OK SENT NID:2557
```

Room members receive:

```text
MSG ROOM study Pasidu Hello everyone
```

Only members of the room receive the room message.

---

## 10. Leave a Room

Enter:

```text
LEAVE study
```

Successful response:

```text
OK LEFT study NID:2557
```

When the final member leaves, the empty room is removed.

---

## 11. Send a File to a User

The client provides the `/send` command for file transfer.

Syntax:

```text
/send <username> <local-path>
```

Example:

```text
/send Amal /tmp/test.txt
```

The client displays output similar to:

```text
Queued file: test.txt -> Amal
```

After the server receives the file successfully:

```text
Server: OK FILE_RECEIVED test.txt NID:2557
```

The receiving client stores the file under its `downloads` directory.

Example:

```text
downloads/Amal/Pasidu/test.txt
```

The server stores the received file under the personalised storage path:

```text
storage/IT21255724/Pasidu/test.txt
```

The maximum accepted file size is **1 MiB**.

---

## 12. Send a File to a Room

First, the users should join the room:

```text
JOIN study
```

Then send a file using the room name as the target:

```text
/send study /tmp/report.pdf
```

The file is delivered to the active members of the room.

---

## 13. Presence Notifications

When a user registers successfully, connected registered clients receive:

```text
MSG JOIN <username>
```

Example:

```text
MSG JOIN Amal
```

When a registered user disconnects:

```text
MSG LEAVE <username>
```

Example:

```text
MSG LEAVE Amal
```

The assignment requires presence notifications but does not specify their exact message format. `MSG JOIN` and `MSG LEAVE` are the formats used by this implementation.

---

## 14. Quit the Application

To disconnect cleanly, enter:

```text
QUIT
```

The server responds:

```text
OK BYE NID:2557
```

The client then closes the connection.

The client can also be closed locally by pressing `Ctrl+D` on an empty input line.

---

# File Transfer Notes

The required client-to-server file-transfer protocol is:

```text
SENDFILE <target> <filename> <filesize>
<exactly filesize raw bytes>
```

The raw file bytes immediately follow the newline terminating the `SENDFILE` command.

The server reads exactly the declared number of bytes before returning to normal command processing.

This allows binary files containing newline, NUL, and other non-text bytes to be transferred correctly.

The server-to-client file delivery format used by this implementation is:

```text
MSG FILE <sender> <filename> <filesize>
<exactly filesize raw bytes>
```

---

# Server Storage

Files received by the server are stored using the required personalised path:

```text
./storage/IT21255724/<sender_username>/<filename>
```

Example:

```text
./storage/IT21255724/Pasidu/report.pdf
```

---

# Server Log

The server writes timestamped activity to:

```text
netmsg_IT21255724.log
```

The log records events such as:

- client connections
- registrations
- commands
- protocol errors
- file transfers
- client disconnections

---

# Project Files

```text
server_5724.c
client_5724.c
config_5724.h
file_io_5724.h
Makefile_5724
README.md
docs/
```

Runtime files such as compiled executables, `downloads/`, `storage/`, and the server log are excluded from normal Git tracking.

---

# Submission Archive

The personalised submission archive name is:

```text
IE3010_IT21255724.zip
```
