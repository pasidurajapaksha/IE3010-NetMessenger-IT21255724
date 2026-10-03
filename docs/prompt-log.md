# AI Prompt Log

Registration number: IT21255724
Tool: OpenAI Codex

This log records substantive AI assistance and how I used it.
Earlier interactions are summarised retrospectively below.

## Socket programming lessons

Prompt summary:
I requested beginner explanations of socket programming,
including socket addresses, socket(), connect(), bind(),
listen(), accept(), send(), recv(), ssize_t, fgets(), and loops.

Assistance received:
Explanations and example code for a TCP client and echo server.

How I used it:
I typed and ran the programs in Linux and shared the code and
terminal output for feedback. I tested repeated messages and
a local quit command.

## Long input behaviour

Prompt summary:
I shared output where a long input line was sent in several
pieces, including chunks of 99 bytes.

Assistance received:
An explanation of fgets() limits, leftover input, newline
characters, and null terminators.

How I used it:
I used the explanation to understand the observed output.
Long-line handling remains to be implemented.

## Partial transfers

Prompt summary:
I asked for the next lesson and whether partial-transfer
handling appeared in the lecture slides.

Assistance received:
An example loop for handling partial sends and references to
Lecture 02 slides 26–28.

How I used it:
I reviewed the explanation. The copied baseline programs still
use one send() call per received or entered chunk.

## Assignment planning and repository setup

Prompt summary:
I asked about assignment requirements, report contents,
GitHub setup, commits, authentication, and repository files.

Assistance received:
A requirements breakdown, README text, Git commands, and
guidance on token permissions and commit-email privacy.

How I used it:
I created the repository, edited the README, configured Git,
and successfully pushed the README commit after fixing the
authentication and email-privacy issues.

## Documentation setup

Prompt summary:
I asked to continue with the required repository documentation.

Assistance received:
Draft structures and starting text for this prompt log and
the design diary.

How I used it:
I reviewed these records against my actual work and saved
them as project documentation.
