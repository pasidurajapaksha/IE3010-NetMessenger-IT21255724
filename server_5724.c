#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "config_5724.h"
#include "file_io_5724.h"
#include <time.h>

#define MAX_CLIENTS 16
#define MAX_ROOMS 16
#define COMMAND_SIZE 1024
#define OUTPUT_SIZE FILE_QUEUE_SIZE

#define RATE_LIMIT_MESSAGES 10U
#define RATE_LIMIT_WINDOW_SECONDS 5

/* Each client has independent registration and buffering state. */
typedef struct
{
    int fd;
    int registered;
    int close_after_output;
    int drop_pending;
    unsigned long session_id;

    /* Optional extension: per-client chat flood protection. */
    time_t rate_window_start;
    unsigned int rate_message_count;

    /* Bounded in-memory upload; disk publication happens only on completion. */
    unsigned char *file_data;
    size_t file_size, file_received;
    char file_name[128];
    unsigned long file_recipients[MAX_CLIENTS];
    time_t upload_activity;

    char username[32];

    char command[COMMAND_SIZE];
    size_t command_length;

    char output[OUTPUT_SIZE];
    size_t output_length;
} Client;

static Client clients[MAX_CLIENTS];
static unsigned long next_session_id;

typedef struct
{
    int active;
    char name[32];
    unsigned char members[MAX_CLIENTS];
} Room;

static Room rooms[MAX_ROOMS];

static void remove_client_from_rooms(const Client *client);

static void announce_presence(const char *event, const char *username);

/* Append each event immediately so evidence survives a stopped server. */
static FILE *server_log;

static void close_server_log(void)
{
    if (server_log) fclose(server_log);
}

static void log_event(const char *format, ...)
{
    if (!server_log) return;
    time_t now = time(NULL);
    struct tm local;
    char timestamp[48] = "unknown-time";
    if (localtime_r(&now, &local))
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &local);

    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    fprintf(server_log, "[%s] ", timestamp);
    /* Escape control characters so message text cannot forge log entries. */
    for (const unsigned char *p = (const unsigned char *)message; *p; p++)
    {
        if (*p < 32 || *p == 127) fprintf(server_log, "\\x%02x", *p);
        else fputc(*p, server_log);
    }
    fputc('\n', server_log);
    if (fflush(server_log) == EOF)
        perror("write server log");
}

/* Prevent socket operations from blocking the entire server. */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
    {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Clear the record before announcing departure, preventing duplicate
   notifications or routing to a connection that is already closed. */
static void disconnect_client(Client *client)
{
    if (client->fd == -1)
    {
        return;
    }

    log_event("DISCONNECT fd=%d user=%s", client->fd,
              client->registered ? client->username : "unregistered");
    if (client->file_data)
        log_event("FILE_ABORT user=%s file=%s received=%zu expected=%zu",
                  client->username, client->file_name,
                  client->file_received, client->file_size);

    int was_registered = client->registered;
    char username[sizeof(client->username)];
    memcpy(username, client->username, sizeof(username));

    printf("Disconnected: %s (fd=%d)\n",
           was_registered ? username : "unregistered", client->fd);

    if (close(client->fd) == -1)
    {
        perror("close client");
    }

    free(client->file_data);
    client->file_data = NULL;
    remove_client_from_rooms(client);

    memset(client, 0, sizeof(*client));
    client->fd = -1;

    if (was_registered)
    {
        announce_presence("LEAVE", username);
    }
}

/* Queue protocol output for transmission when the socket is writable. */
static int queue_response(Client *client, const char *response)
{
    if (!strncmp(response, "ERR ", 4))
        log_event("ERROR fd=%d user=%s response=%s", client->fd,
                  client->registered ? client->username : "unregistered", response);
    size_t length = strlen(response);

    if (length > sizeof(client->output) - client->output_length)
    {
        fprintf(stderr, "Output queue full for fd=%d\n", client->fd);
        return -1;
    }

    memcpy(client->output + client->output_length, response, length);
    client->output_length += length;

    return 0;
}

/* Optional extension: allow at most 10 accepted chat messages per 5-second
   window for each client. Non-chat commands and invalid requests are not
   charged against the allowance. */
static int allow_chat_message(Client *client)
{
    time_t now = time(NULL);

    if (client->rate_window_start == 0 ||
        now < client->rate_window_start ||
        now - client->rate_window_start >= RATE_LIMIT_WINDOW_SECONDS)
    {
        client->rate_window_start = now;
        client->rate_message_count = 0;
    }

    if (client->rate_message_count >= RATE_LIMIT_MESSAGES)
    {
        log_event("RATE_LIMIT fd=%d user=%s count=%u",
                  client->fd,
                  client->registered ? client->username : "unregistered",
                  client->rate_message_count);
        return 0;
    }

    client->rate_message_count++;
    return 1;
}

/* The brief requires presence notifications but does not define their syntax.
   Our documented choice is MSG JOIN <name> and MSG LEAVE <name>, without NID. */
static void announce_presence(const char *event, const char *username)
{
    char notification[80];
    int written = snprintf(notification, sizeof(notification),
                           "MSG %s %s\n", event, username);
    if (written < 0 || (size_t)written >= sizeof(notification))
    {
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        Client *recipient = &clients[i];
        if (recipient->fd == -1 || !recipient->registered ||
            recipient->close_after_output || recipient->drop_pending)
        {
            continue;
        }

        if (queue_response(recipient, notification) == -1)
        {
            /* Defer cleanup so every recipient sees this event before
               any leave events caused by output-queue overflow. */
            recipient->drop_pending = 1;
        }
    }
}

/* Clearing a dropped client can queue a LEAVE event and expose another
   full queue. Repeat until no marked connections remain; no recursion. */
static void remove_dropped_clients(void)
{
    int removed;
    do
    {
        removed = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd != -1 && clients[i].drop_pending)
            {
                disconnect_client(&clients[i]);
                removed = 1;
            }
        }
    } while (removed);
}

/* Preserve bytes that send() could not transmit yet. */
static int flush_output(Client *client)
{
    ssize_t sent = send(
        client->fd,
        client->output,
        client->output_length,
        MSG_NOSIGNAL
    );

    if (sent == -1)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }

        perror("send");
        return -1;
    }

    if (sent == 0)
    {
        return -1;
    }

    client->output_length -= (size_t)sent;

    memmove(
        client->output,
        client->output + (size_t)sent,
        client->output_length
    );

    return 0;
}

static int valid_username(const char *name)
{
    size_t length = strlen(name);

    if (length == 0 || length >= sizeof(clients[0].username))
    {
        return 0;
    }

    for (size_t i = 0; i < length; i++)
    {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
        {
            return 0;
        }
    }

    return 1;
}

static int username_taken(const char *name)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 &&
            clients[i].registered &&
            strcmp(clients[i].username, name) == 0)
        {
            return 1;
        }
    }

    return 0;
}

/* Room membership uses client-array indexes, so disconnect cleanup must
   clear membership before a client slot can be reused. */
static int find_room(const char *name)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].active && strcmp(rooms[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void remove_empty_room(int room_index)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (rooms[room_index].members[i])
        {
            return;
        }
    }
    memset(&rooms[room_index], 0, sizeof(rooms[room_index]));
}

static void remove_client_from_rooms(const Client *client)
{
    int slot = (int)(client - clients);
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].active)
        {
            rooms[i].members[slot] = 0;
            remove_empty_room(i);
        }
    }
}

/* JOIN creates the room if needed, then marks this client's membership.
   Room and user names share a namespace to keep future file targets unique. */
static int handle_join(Client *client, const char *name)
{
    if (!valid_username(name))
    {
        return queue_response(client,
            "ERR 015 INVALID_ROOM_NAME " NODE_ID "\n");
    }

    /* A shared user/room namespace avoids ambiguous SENDFILE targets later. */
    if (username_taken(name))
    {
        return queue_response(client,
            "ERR 014 NAME_CONFLICT " NODE_ID "\n");
    }

    int room_index = find_room(name);
    if (room_index == -1)
    {
        for (int i = 0; i < MAX_ROOMS; i++)
        {
            if (!rooms[i].active)
            {
                room_index = i;
                break;
            }
        }
        if (room_index == -1)
        {
            return queue_response(client,
                "ERR 017 ROOM_LIMIT_REACHED " NODE_ID "\n");
        }

        memset(&rooms[room_index], 0, sizeof(rooms[room_index]));
        rooms[room_index].active = 1;
        memcpy(rooms[room_index].name, name, strlen(name) + 1);
    }

    /* Joining an existing membership is harmless and returns the same OK. */
    rooms[room_index].members[client - clients] = 1;
    char response[160];
    snprintf(response, sizeof(response), "OK JOINED %s %s\n", name, NODE_ID);
    return queue_response(client, response);
}

/* LEAVE affects this room only. The final member leaving deletes the room. */
static int handle_leave(Client *client, const char *name)
{
    if (!valid_username(name))
    {
        return queue_response(client,
            "ERR 015 INVALID_ROOM_NAME " NODE_ID "\n");
    }

    int room_index = find_room(name);
    if (room_index == -1)
    {
        return queue_response(client,
            "ERR 003 ROOM_NOT_FOUND " NODE_ID "\n");
    }

    int slot = (int)(client - clients);
    if (!rooms[room_index].members[slot])
    {
        return queue_response(client,
            "ERR 016 NOT_ROOM_MEMBER " NODE_ID "\n");
    }

    char response[160];
    snprintf(response, sizeof(response), "OK LEFT %s %s\n", name, NODE_ID);
    rooms[room_index].members[slot] = 0;
    remove_empty_room(room_index);
    return queue_response(client, response);
}

/* ROOMS is one newline-terminated wire response, including an empty list. */
static int handle_rooms(Client *client)
{
    char response[1024];
    int written = snprintf(response, sizeof(response), "OK ROOMS ");
    if (written < 0 || (size_t)written >= sizeof(response))
    {
        return -1;
    }
    size_t used = (size_t)written;
    int first = 1;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!rooms[i].active)
        {
            continue;
        }
        written = snprintf(response + used, sizeof(response) - used,
                               "%s%s", first ? "" : ",", rooms[i].name);
        if (written < 0 || (size_t)written >= sizeof(response) - used)
        {
            return -1;
        }
        used += (size_t)written;
        first = 0;
    }

    written = snprintf(response + used, sizeof(response) - used,
                           " %s\n", NODE_ID);
    if (written < 0 || (size_t)written >= sizeof(response) - used)
    {
        return -1;
    }
    return queue_response(client, response);
}

/* Split only the room name; retain spaces in the remaining message text.
   The sender must belong to the room and receives a copy as a member. */
static int handle_room_message(Client *sender, const char *arguments)
{
    const char *separator = strchr(arguments, ' ');
    if (separator == NULL || separator == arguments)
    {
        return queue_response(sender,
            "ERR 012 INVALID_ARGUMENTS " NODE_ID "\n");
    }

    char name[32];
    size_t name_length = (size_t)(separator - arguments);
    if (name_length >= sizeof(name))
    {
        return queue_response(sender,
            "ERR 015 INVALID_ROOM_NAME " NODE_ID "\n");
    }
    memcpy(name, arguments, name_length);
    name[name_length] = '\0';
    if (!valid_username(name))
    {
        return queue_response(sender,
            "ERR 015 INVALID_ROOM_NAME " NODE_ID "\n");
    }

    const char *message = separator + 1;
    if (message[0] == '\0')
    {
        return queue_response(sender,
            "ERR 011 EMPTY_MESSAGE " NODE_ID "\n");
    }

    int room_index = find_room(name);
    if (room_index == -1)
    {
        return queue_response(sender,
            "ERR 003 ROOM_NOT_FOUND " NODE_ID "\n");
    }
    if (!rooms[room_index].members[sender - clients])
    {
        return queue_response(sender,
            "ERR 016 NOT_ROOM_MEMBER " NODE_ID "\n");
    }

    if (!allow_chat_message(sender))
    {
        return queue_response(sender,
            "ERR 023 RATE_LIMITED " NODE_ID "\n");
    }

    char forwarded[1200];
    int written = snprintf(forwarded, sizeof(forwarded),
                           "MSG ROOM %s %s %s\n", name,
                           sender->username, message);
    if (written < 0 || (size_t)written >= sizeof(forwarded))
    {
        return -1;
    }

    /* All members, including the sender, receive the room message.
       Reserve the sender's acknowledgement and message together. */
    const char acknowledgement[] = "OK SENT " NODE_ID "\n";
    if ((size_t)written + sizeof(acknowledgement) - 1 >
        sizeof(sender->output) - sender->output_length)
    {
        return -1;
    }
    if (queue_response(sender, acknowledgement) == -1)
    {
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        Client *recipient = &clients[i];
        if (!rooms[room_index].members[i] || recipient->fd == -1 ||
            !recipient->registered || recipient->close_after_output ||
            recipient->drop_pending)
        {
            continue;
        }
        if (queue_response(recipient, forwarded) == -1)
        {
            /* Defer cleanup until routing finishes; cleanup may delete rooms. */
            recipient->drop_pending = 1;
        }
    }
    return 0;
}

/* Build the required single-line, comma-separated user-list response. */
static int handle_list(Client *client)
{
    char response[1024];
    size_t used;
    int first_user = 1;

    int written = snprintf(response, sizeof(response), "OK USERS ");

    if (written < 0 || (size_t)written >= sizeof(response))
    {
        return -1;
    }

    used = (size_t)written;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd == -1 || !clients[i].registered ||
            clients[i].close_after_output || clients[i].drop_pending)
        {
            continue;
        }

        written = snprintf(
            response + used,
            sizeof(response) - used,
            "%s%s",
            first_user ? "" : ",",
            clients[i].username
        );

        if (written < 0 ||
            (size_t)written >= sizeof(response) - used)
        {
            return -1;
        }

        used += (size_t)written;
        first_user = 0;
    }

    written = snprintf(
        response + used,
        sizeof(response) - used,
        " %s\n",
        NODE_ID
    );

    if (written < 0 ||
        (size_t)written >= sizeof(response) - used)
    {
        return -1;
    }

    return queue_response(client, response);
}

/* Deliver a broadcast to every other eligible registered client. */
static int handle_broadcast(Client *sender, const char *message)
{
    if (message[0] == '\0')
    {
        return queue_response(
            sender,
            "ERR 011 EMPTY_MESSAGE " NODE_ID "\n"
        );
    }

    if (!allow_chat_message(sender))
    {
        return queue_response(
            sender,
            "ERR 023 RATE_LIMITED " NODE_ID "\n"
        );
    }

    char forwarded[1200];

    int written = snprintf(
        forwarded,
        sizeof(forwarded),
        "MSG BCAST %s %s\n",
        sender->username,
        message
    );

    if (written < 0 || (size_t)written >= sizeof(forwarded))
    {
        return -1;
    }

    if (queue_response(sender, "OK SENT " NODE_ID "\n") == -1)
    {
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        Client *recipient = &clients[i];

        if (recipient->fd == -1 ||
            !recipient->registered ||
            recipient->close_after_output ||
            recipient->drop_pending ||
            recipient == sender)
        {
            continue;
        }

        /* Disconnect recipients whose bounded output queue is full. */
        if (queue_response(recipient, forwarded) == -1)
        {
            disconnect_client(recipient);
        }
    }

    return 0;
}

/* Route a private message to one registered user, preserving message spaces. */
static int handle_private(Client *sender, const char *arguments)
{
    const char *separator = strchr(arguments, ' ');
    if (separator == NULL || separator == arguments)
    {
        return queue_response(sender,
            "ERR 012 INVALID_ARGUMENTS " NODE_ID "\n");
    }

    size_t name_length = (size_t)(separator - arguments);
    char target[sizeof(sender->username)];
    if (name_length >= sizeof(target))
    {
        return queue_response(sender,
            "ERR 006 INVALID_USERNAME " NODE_ID "\n");
    }

    memcpy(target, arguments, name_length);
    target[name_length] = '\0';
    if (!valid_username(target))
    {
        return queue_response(sender,
            "ERR 006 INVALID_USERNAME " NODE_ID "\n");
    }

    const char *message = separator + 1;
    if (message[0] == '\0')
    {
        return queue_response(sender,
            "ERR 011 EMPTY_MESSAGE " NODE_ID "\n");
    }

    Client *recipient = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 &&
            clients[i].registered &&
            !clients[i].close_after_output &&
            !clients[i].drop_pending &&
            strcmp(clients[i].username, target) == 0)
        {
            recipient = &clients[i];
            break;
        }
    }

    if (recipient == NULL)
    {
        return queue_response(sender,
            "ERR 002 USER_NOT_FOUND " NODE_ID "\n");
    }

    if (!allow_chat_message(sender))
    {
        return queue_response(sender,
            "ERR 023 RATE_LIMITED " NODE_ID "\n");
    }

    char forwarded[1200];
    int written = snprintf(forwarded, sizeof(forwarded),
                           "MSG PRIV %s %s\n", sender->username, message);
    if (written < 0 || (size_t)written >= sizeof(forwarded))
    {
        return -1;
    }

    const char acknowledgement[] = "OK SENT " NODE_ID "\n";
    size_t ack_length = sizeof(acknowledgement) - 1;
    size_t message_length = (size_t)written;

    /* Reserve capacity for both outputs before accepting the message.
       Self-messaging is allowed: both lines then share the same queue. */
    if (ack_length > sizeof(sender->output) - sender->output_length)
    {
        return -1;
    }

    size_t required = message_length;
    if (recipient == sender)
    {
        required += ack_length;
    }

    if (required > sizeof(recipient->output) - recipient->output_length)
    {
        return queue_response(sender,
            "ERR 013 RECIPIENT_UNAVAILABLE " NODE_ID "\n");
    }

    /* The event loop is single-threaded, so capacity cannot change here.
       OK SENT means queued for delivery, not confirmed read by the user. */
    if (queue_response(sender, acknowledgement) == -1)
    {
        return -1;
    }

    return queue_response(recipient, forwarded);
}

/* Raw payload state is independent of newline command framing. */
static void clear_upload(Client *client)
{
    free(client->file_data);
    client->file_data = NULL;
    client->file_size = client->file_received = 0;
    client->upload_activity = 0;
    memset(client->file_recipients, 0, sizeof(client->file_recipients));
}

static int reject_upload(Client *client, const char *response)
{
    /* Payload follows the header immediately. Close after error rather than
       accidentally treating rejected binary bytes as new text commands. */
    clear_upload(client);
    client->close_after_output = 1;
    return queue_response(client, response);
}

static int finish_upload(Client *sender)
{
    char header[256], acknowledgement[200];
    /* Recipient framing is our choice because the brief leaves it unspecified:
       MSG FILE <sender> <filename> <size> newline, then exactly size raw bytes. */
    int h = snprintf(header, sizeof(header), "MSG FILE %s %s %zu\n",
                     sender->username, sender->file_name, sender->file_size);
    int a = snprintf(acknowledgement, sizeof(acknowledgement),
                     "OK FILE_RECEIVED %s " NODE_ID "\n", sender->file_name);
    if (h < 0 || a < 0 || (size_t)h >= sizeof(header) ||
        (size_t)a >= sizeof(acknowledgement)) return -1;

    /* Snapshot recipients at upload start. Session IDs prevent a reused slot
       from receiving a previous client's file. All intended recipients must
       still be available when the upload completes. */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        Client *r = &clients[i];
        if (!sender->file_recipients[i]) continue;
        size_t required = (size_t)h + sender->file_size;
        if (r == sender) required += (size_t)a;
        if (r->fd == -1 || r->session_id != sender->file_recipients[i] ||
            r->close_after_output || r->drop_pending ||
            required > sizeof(r->output) - r->output_length)
        {
            clear_upload(sender);
            return queue_response(sender,
                "ERR 013 RECIPIENT_UNAVAILABLE " NODE_ID "\n");
        }
    }
    if ((size_t)a > sizeof(sender->output) - sender->output_length)
        return -1;

    if (file_save("storage", REGISTRATION_NUMBER, sender->username,
                  sender->file_name, sender->file_data, sender->file_size) == -1)
    {
        int exists = errno == EEXIST;
        perror("store upload");
        clear_upload(sender);
        return queue_response(sender, exists ?
            "ERR 018 FILE_EXISTS " NODE_ID "\n" :
            "ERR 019 FILE_IO_ERROR " NODE_ID "\n");
    }

    queue_response(sender, acknowledgement);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!sender->file_recipients[i]) continue;
        Client *r = &clients[i];
        /* Append header and payload together: chat cannot interleave them. */
        memcpy(r->output + r->output_length, header, (size_t)h);
        r->output_length += (size_t)h;
        memcpy(r->output + r->output_length, sender->file_data, sender->file_size);
        r->output_length += sender->file_size;
    }
    log_event("FILE_STORED_AND_QUEUED user=%s file=%s bytes=%zu",
              sender->username, sender->file_name, sender->file_size);
    printf("Stored file: %s/%s/%s (%zu bytes)\n",
           STORAGE_ROOT, sender->username, sender->file_name, sender->file_size);
    clear_upload(sender);
    return 0;
}

static int begin_upload(Client *sender, const char *arguments)
{
    char target[32], filename[128], size_text[32], extra;
    if (sscanf(arguments, "%31s %127s %31s %c",
               target, filename, size_text, &extra) != 3 ||
        !valid_username(target) || !file_component_valid(filename))
        return reject_upload(sender, "ERR 012 INVALID_ARGUMENTS " NODE_ID "\n");

    size_t size;
    int parsed = file_size_parse(size_text, &size);
    if (parsed != 0)
        return reject_upload(sender, parsed == 1 ?
            "ERR 004 FILE_TOO_LARGE " NODE_ID "\n" :
            "ERR 020 INVALID_FILE_SIZE " NODE_ID "\n");

    memset(sender->file_recipients, 0, sizeof(sender->file_recipients));
    int room = find_room(target);
    int recipients = 0;
    if (room != -1)
    {
        if (!rooms[room].members[sender - clients])
            return reject_upload(sender, "ERR 016 NOT_ROOM_MEMBER " NODE_ID "\n");
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (rooms[room].members[i] && clients[i].fd != -1 &&
                clients[i].registered && !clients[i].close_after_output &&
                !clients[i].drop_pending)
            {
                sender->file_recipients[i] = clients[i].session_id;
                recipients++;
            }
        }
    }
    else
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd != -1 && clients[i].registered &&
                !clients[i].close_after_output && !clients[i].drop_pending &&
                strcmp(clients[i].username, target) == 0)
            {
                sender->file_recipients[i] = clients[i].session_id;
                recipients++;
                break;
            }
        }
    }
    /* Missing bare targets have no type prefix: use USER_NOT_FOUND. */
    if (!recipients)
        return reject_upload(sender, "ERR 002 USER_NOT_FOUND " NODE_ID "\n");

    sender->file_data = malloc(size ? size : 1);
    if (!sender->file_data)
        return reject_upload(sender, "ERR 021 RESOURCE_LIMIT " NODE_ID "\n");
    memcpy(sender->file_name, filename, strlen(filename) + 1);
    sender->file_size = size;
    sender->file_received = 0;
    sender->upload_activity = time(NULL);
    log_event("FILE_START user=%s target=%s file=%s bytes=%zu recipients=%d",
              sender->username, target, filename, size, recipients);
    return size == 0 ? finish_upload(sender) : 0;
}

/* Dispatch one complete command. */
static int handle_command(Client *client, const char *command)
{
    /* Includes broadcast/private/room requests and transfer headers. */
    log_event("COMMAND fd=%d user=%s text=%s", client->fd,
              client->registered ? client->username : "unregistered", command);

    if (strncmp(command, "REGISTER ", 9) == 0)
    {
        const char *name = command + 9;

        if (client->registered)
        {
            return queue_response(
                client,
                "ERR 005 ALREADY_REGISTERED " NODE_ID "\n"
            );
        }

        if (!valid_username(name))
        {
            return queue_response(
                client,
                "ERR 006 INVALID_USERNAME " NODE_ID "\n"
            );
        }

        if (username_taken(name))
        {
            return queue_response(
                client,
                "ERR 001 USERNAME_TAKEN " NODE_ID "\n"
            );
        }

        if (find_room(name) != -1)
        {
            return queue_response(client,
                "ERR 014 NAME_CONFLICT " NODE_ID "\n");
        }

        char response[160];
        snprintf(response, sizeof(response),
                 "OK REGISTERED %s %s\n", name, NODE_ID);

        /* Queue the acknowledgement first. A failed registration must
           not generate JOIN or LEAVE events for a never-announced user. */
        if (queue_response(client, response) == -1)
        {
            return -1;
        }

        memcpy(client->username, name, strlen(name) + 1);
        client->registered = 1;

        printf("Registered: %s (fd=%d)\n",
               client->username, client->fd);

        /* Includes the newly registered client, after its OK response. */
        log_event("REGISTER fd=%d user=%s", client->fd, client->username);
        announce_presence("JOIN", client->username);
        return 0;
    }

    if (strcmp(command, "REGISTER") == 0)
    {
        return queue_response(
            client,
            "ERR 006 INVALID_USERNAME " NODE_ID "\n"
        );
    }

    /* Reject before reading raw bytes if the sender has not registered. */
    if (!client->registered &&
        (!strcmp(command, "SENDFILE") || !strncmp(command, "SENDFILE ", 9)))
        return reject_upload(client, "ERR 007 REGISTER_REQUIRED " NODE_ID "\n");

    /* Other commands require successful registration first. */
    if (!client->registered)
    {
        return queue_response(
            client,
            "ERR 007 REGISTER_REQUIRED " NODE_ID "\n"
        );
    }

    if (strcmp(command, "LIST") == 0)
    {
        return handle_list(client);
    }

    if (strcmp(command, "BCAST") == 0)
    {
        return handle_broadcast(client, "");
    }

    if (strncmp(command, "BCAST ", 6) == 0)
    {
        return handle_broadcast(client, command + 6);
    }

    if (strcmp(command, "PMSG") == 0)
    {
        return queue_response(client,
            "ERR 012 INVALID_ARGUMENTS " NODE_ID "\n");
    }

    if (strncmp(command, "PMSG ", 5) == 0)
    {
        return handle_private(client, command + 5);
    }

    if (strcmp(command, "JOIN") == 0 ||
        strcmp(command, "LEAVE") == 0 ||
        strcmp(command, "RMSG") == 0)
    {
        return queue_response(client,
            "ERR 012 INVALID_ARGUMENTS " NODE_ID "\n");
    }
    if (strncmp(command, "JOIN ", 5) == 0)
    {
        return handle_join(client, command + 5);
    }
    if (strncmp(command, "LEAVE ", 6) == 0)
    {
        return handle_leave(client, command + 6);
    }
    if (strcmp(command, "ROOMS") == 0)
    {
        return handle_rooms(client);
    }
    if (strncmp(command, "RMSG ", 5) == 0)
    {
        return handle_room_message(client, command + 5);
    }

    if (!strcmp(command, "SENDFILE"))
        return reject_upload(client, "ERR 012 INVALID_ARGUMENTS " NODE_ID "\n");
    if (!strncmp(command, "SENDFILE ", 9))
        return begin_upload(client, command + 9);

    if (strcmp(command, "QUIT") == 0)
    {
        /* Stop accepting commands and close after sending queued output. */
        client->close_after_output = 1;

        return queue_response(
            client,
            "OK BYE " NODE_ID "\n"
        );
    }

    return queue_response(
        client,
        "ERR 008 UNKNOWN_COMMAND " NODE_ID "\n"
    );
}

/* Reconstruct newline-terminated commands across TCP receive boundaries. */
static int receive_commands(Client *client)
{
    char buffer[8192];

    ssize_t received = recv(
        client->fd,
        buffer,
        sizeof(buffer),
        0
    );

    if (received == -1)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }

        perror("recv");
        return -1;
    }

    if (received == 0)
    {
        return -1;
    }

    for (ssize_t i = 0; i < received; i++)
    {
        /* Binary payload may contain newlines and NUL bytes. Consume exactly
           the declared size, then resume command parsing in this same buffer. */
        if (client->file_data)
        {
            size_t take = client->file_size - client->file_received;
            size_t available = (size_t)(received - i);
            if (take > available) take = available;
            memcpy(client->file_data + client->file_received, buffer + i, take);
            client->file_received += take;
            client->upload_activity = time(NULL);
            i += (ssize_t)take - 1;
            if (client->file_received == client->file_size &&
                finish_upload(client) == -1)
                return -1;
            continue;
        }

        char current = buffer[i];

        if (current == '\n')
        {
            client->command[client->command_length] = '\0';

            printf("fd=%d command: %s\n",
                   client->fd, client->command);

            if (handle_command(client, client->command) == -1)
            {
                return -1;
            }

            client->command_length = 0;

            if (client->close_after_output || client->drop_pending)
            {
                return 0;
            }
        }
        else if (current == '\0')
        {
            client->close_after_output = 1;

            return queue_response(
                client,
                "ERR 009 INVALID_COMMAND " NODE_ID "\n"
            );
        }
        else if (client->command_length < sizeof(client->command) - 1)
        {
            client->command[client->command_length++] = current;
        }
        else
        {
            client->close_after_output = 1;

            return queue_response(
                client,
                "ERR 010 COMMAND_TOO_LONG " NODE_ID "\n"
            );
        }
    }

    return 0;
}

/* Accept one pending connection into a free client slot. */
static void accept_client(int listen_fd)
{
    struct sockaddr_in address = {0};
    socklen_t address_length = sizeof(address);

    int fd = accept(
        listen_fd,
        (struct sockaddr *)&address,
        &address_length
    );

    if (fd == -1)
    {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror("accept");
        }

        return;
    }

    if (fd >= FD_SETSIZE)
    {
        fprintf(stderr, "Client descriptor exceeds select limit.\n");
        close(fd);
        return;
    }

    int slot = -1;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd == -1)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        fprintf(stderr, "Client capacity reached; connection closed.\n");
        close(fd);
        return;
    }

    if (set_nonblocking(fd) == -1)
    {
        perror("fcntl client");
        close(fd);
        return;
    }

    memset(&clients[slot], 0, sizeof(clients[slot]));
    clients[slot].fd = fd;
    clients[slot].session_id = ++next_session_id;
    log_event("CONNECT fd=%d session=%lu", fd, clients[slot].session_id);

    char ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip)) != NULL)
    {
        printf("Connected: %s:%u (fd=%d)\n",
               ip,
               (unsigned int)ntohs(address.sin_port),
               fd);
    }
    else
    {
        printf("Connected: fd=%d\n", fd);
    }
}

int main(void)
{
    /* Failure to open the required log is reported instead of silently
       running without assignment evidence. */
    server_log = fopen(LOG_FILE, "a");
    if (!server_log)
    {
        perror("open server log");
        return EXIT_FAILURE;
    }
    atexit(close_server_log);
    log_event("START registration=%s port=%d", REGISTRATION_NUMBER, SERVER_PORT);

    /* A descriptor of -1 identifies an unused slot. */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (listen_fd >= FD_SETSIZE)
    {
        fprintf(stderr, "Listener descriptor exceeds select limit.\n");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    int reuse_address = 1;

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1)
    {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_BIND_IP, &address.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server binding address\n");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (bind(listen_fd, (struct sockaddr *)&address,
             sizeof(address)) == -1)
    {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 16) == -1)
    {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (set_nonblocking(listen_fd) == -1)
    {
        perror("fcntl listener");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("NetMessenger server | %s\n", REGISTRATION_NUMBER);
    printf("Listening on %s:%d\n", SERVER_BIND_IP, SERVER_PORT);
    printf("Client capacity: %d\n", MAX_CLIENTS);
    printf("File limit: %u bytes; upload idle timeout: 30 seconds\n", MAX_FILE_SIZE);
    fflush(stdout);

    for (;;)
    {
        remove_dropped_clients();
        /* A stalled upload must not retain its buffer indefinitely. */
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd != -1 && clients[i].file_data &&
                time(NULL) - clients[i].upload_activity >= 30)
            {
                if (reject_upload(&clients[i],
                    "ERR 022 TRANSFER_TIMEOUT " NODE_ID "\n") == -1)
                    disconnect_client(&clients[i]);
            }
        }

        fd_set read_fds;
        fd_set write_fds;

        /* select() modifies these sets, so rebuild them each iteration. */
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        FD_SET(listen_fd, &read_fds);
        int highest_fd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            Client *client = &clients[i];

            if (client->fd == -1 || client->drop_pending)
            {
                continue;
            }

            if (!client->close_after_output)
            {
                FD_SET(client->fd, &read_fds);
            }

            if (client->output_length > 0)
            {
                FD_SET(client->fd, &write_fds);
            }

            if (client->fd > highest_fd)
            {
                highest_fd = client->fd;
            }
        }

        struct timeval timeout = {1, 0};
        int ready = select(
            highest_fd + 1,
            &read_fds,
            &write_fds,
            NULL,
            &timeout
        );

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            break;
        }

        if (FD_ISSET(listen_fd, &read_fds))
        {
            accept_client(listen_fd);
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            Client *client = &clients[i];

            if (client->fd == -1 || client->drop_pending)
            {
                continue;
            }

            if (FD_ISSET(client->fd, &read_fds))
            {
                if (receive_commands(client) == -1)
                {
                    disconnect_client(client);
                    continue;
                }
            }

            if (FD_ISSET(client->fd, &write_fds) &&
                client->output_length > 0)
            {
                if (flush_output(client) == -1)
                {
                    disconnect_client(client);
                    continue;
                }
            }

            if (client->close_after_output &&
                client->output_length == 0)
            {
                disconnect_client(client);
            }
        }
    }

    /* Clean up after an unrecoverable select() failure. */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1)
        {
            disconnect_client(&clients[i]);
        }
    }

    close(listen_fd);
    return EXIT_FAILURE;
}
