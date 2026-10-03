# Design Diary

Registration number: IT21255724

## 3 October 2026 — Initial setup and TCP practice

I created the GitHub repository and configured Git in my Linux
environment. I added the project description and personalised
assignment values to the README.

I copied my practice client and server into the repository using
the required filenames: client_5724.c and server_5724.c.

The current programs use TCP on loopback address 127.0.0.1 and
practice port 8080. The server handles one connected client and
echoes received bytes. The client reads keyboard input using
fgets() and supports repeated exchanges. Entering lowercase
quit closes the client locally.

During testing, long keyboard input was processed in pieces
because the input array holds 100 bytes, including the string
terminator.

The current implementation is a learning baseline. It does not
yet implement the assignment protocol or multiple simultaneous
clients.

Next steps:
- Change the port to the personalised value 11724.
- Improve sending, receiving, and disconnect handling.
- Add the required Makefile.
- Implement newline-based command handling before chat features.
