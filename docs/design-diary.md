# NetMessenger Design Diary

**Registration Number:** IT21255724  
**Module:** IE3010 — Network Programming

## 3 October 2026 — Initial Setup

I reviewed the assignment requirements and calculated the personalised values from my registration number IT21255724. The last four digits are 5724, giving server port `6000 + 5724 = 11724`. Digits 3–6 of the numeric part 21255724 are 2557, giving `NID:2557`.

I created the initial C client and server using BSD sockets, added the personalised Makefile, README, and Git repository, and tested a basic TCP connection. During setup, GitHub rejected a push because the commit metadata contained my private email address. I changed the repository configuration to use my GitHub noreply address and corrected the unpublished commit.

## 4 October 2026 — TCP Framing and Connection Handling

I improved the communication logic so the server did not assume that one `recv()` call contained exactly one command. Since TCP is a byte stream, I added per-client command buffering so partial commands are retained until a newline is received and multiple commands can be processed correctly.

I also improved disconnect handling so closed client sockets could be removed without terminating the server.

## 5 October 2026 — Multi-Client Messaging and Rooms

I implemented `REGISTER`, `LIST`, `QUIT`, broadcast messaging, private messaging, and multi-client support using `select()`.

I chose a single-process `select()`-based design so one server could handle multiple clients without creating a separate thread or process for each connection.

Presence notifications were implemented using:

`MSG JOIN <username>`  
`MSG LEAVE <username>`

The assignment requires presence notifications but does not define their exact format, so this was an implementation choice.

I then implemented `JOIN`, `LEAVE`, `ROOMS`, and `RMSG`. Room membership is tied to active client slots, and empty rooms are removed automatically. Usernames and room names share a namespace to avoid ambiguity for file-transfer targets.

## 5 October 2026 — File Transfer and Logging

I implemented direct and room file transfer using `SENDFILE`.

The main challenge was separating raw binary file data from newline-delimited commands. The server therefore switches into a file-receive state and reads exactly the declared number of bytes before returning to normal command parsing. This allows files containing newline and NUL bytes to be transferred correctly.

Received files are stored under:

`./storage/IT21255724/<sender_username>/<filename>`

I also added timestamped server logging to `netmsg_IT21255724.log`.

## 6 October 2026 — Testing and Optional Extension

I completed integration and robustness testing. The project compiled cleanly with `-Wall -Wextra -Wpedantic`.

Testing included five simultaneous clients, messaging, rooms, presence, direct and room file transfer, graceful and abrupt disconnects, duplicate usernames, error handling, partial TCP commands, multiple commands in one input burst, zero-byte files, oversized-file rejection, interrupted transfers, and binary file transfer. SHA-256 hashes of the original binary file, server-stored copy, and recipient-downloaded copy were identical.

After the mandatory functionality was stable, I implemented basic per-client rate limiting for `BCAST`, `PMSG`, and `RMSG` on a separate Git branch. A client may submit up to 10 chat-message requests within a five-second window; further requests receive `ERR 023 RATE_LIMITED NID:2557` until the window resets.

Before merging the optional feature, I ran a full automated regression and smoke test. I repeated the same test after merging into `main`. The final result was `19 PASS / 0 FAIL / 19 TOTAL`.
