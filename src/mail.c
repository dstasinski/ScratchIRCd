/**
 * @file mail.c
 * @brief Dedicated worker-thread delivery through a sendmail-compatible MTA.
 */

#include "mail.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
}

static int read_full(int fd, void *buffer, size_t length) {
    unsigned char *cursor = buffer;
    while (length > 0U) {
        ssize_t n = read(fd, cursor, length);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += (size_t)n;
        length -= (size_t)n;
    }
    return 1;
}

static int write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *cursor = buffer;
    while (length > 0U) {
        ssize_t n = write(fd, cursor, length);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += (size_t)n;
        length -= (size_t)n;
    }
    return 0;
}

/**
 * Deliver one message without a shell. Header values are generated from
 * validated configuration/account fields by the caller, preventing command
 * injection through the MTA invocation.
 */
static void deliver_one(const MailSender *sender, const MailRequest *request) {
    int input_pipe[2] = {-1, -1};
    pid_t child;
    char message[IRCD_MAIL_BODY_MAX + IRCD_EMAIL_MAX * 2U +
                 IRCD_MAIL_SUBJECT_MAX + 256U];
    int length;

    if (sender == NULL || request == NULL || sender->sendmail_path[0] == '\0') return;
    if (pipe(input_pipe) != 0) return;

    child = fork();
    if (child == 0) {
        (void)dup2(input_pipe[0], STDIN_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        execl(sender->sendmail_path, sender->sendmail_path, "-t", "-i", (char *)NULL);
        _exit(127);
    }
    if (child < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        return;
    }

    close(input_pipe[0]);
    length = snprintf(message, sizeof(message),
                      "To: %s\nFrom: %s\nSubject: %s\n"
                      "MIME-Version: 1.0\n"
                      "Content-Type: text/plain; charset=UTF-8\n"
                      "Content-Transfer-Encoding: 8bit\n\n%s\n",
                      request->to, request->from, request->subject, request->body);
    if (length > 0 && (size_t)length < sizeof(message))
        (void)write_full(input_pipe[1], message, (size_t)length);
    close(input_pipe[1]);
    while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        /* retry */
    }
}

static void *mail_thread(void *arg) {
    MailSender *sender = arg;
    MailRequest request;
    while (read_full(sender->request_read_fd, &request, sizeof(request)) == 1)
        deliver_one(sender, &request);
    return NULL;
}

int mail_sender_init(MailSender *sender, const char *sendmail_path) {
    int requests[2] = {-1, -1};

    if (sender == NULL) return -1;
    memset(sender, 0, sizeof(*sender));
    sender->request_read_fd = -1;
    sender->request_write_fd = -1;

    if (sendmail_path == NULL || *sendmail_path == '\0') return 0;
    if (strlen(sendmail_path) >= sizeof(sender->sendmail_path)) return -1;
    (void)snprintf(sender->sendmail_path, sizeof(sender->sendmail_path), "%s", sendmail_path);

    if (pipe(requests) != 0) return -1;
    sender->request_read_fd = requests[0];
    sender->request_write_fd = requests[1];
    if (set_nonblocking(sender->request_write_fd) != 0) {
        mail_sender_destroy(sender);
        return -1;
    }
    if (pthread_create(&sender->thread, NULL, mail_thread, sender) != 0) {
        mail_sender_destroy(sender);
        return -1;
    }
    sender->running = 1;
    return 0;
}

void mail_sender_destroy(MailSender *sender) {
    if (sender == NULL) return;
    if (sender->request_write_fd >= 0) {
        close(sender->request_write_fd);
        sender->request_write_fd = -1;
    }
    if (sender->running) {
        (void)pthread_join(sender->thread, NULL);
        sender->running = 0;
    }
    if (sender->request_read_fd >= 0) {
        close(sender->request_read_fd);
        sender->request_read_fd = -1;
    }
}

int mail_sender_submit(MailSender *sender, const MailRequest *request) {
    ssize_t n;
    if (sender == NULL || request == NULL || !sender->running ||
        sender->request_write_fd < 0) return -1;
    do {
        n = write(sender->request_write_fd, request, sizeof(*request));
    } while (n < 0 && errno == EINTR);
    return n == (ssize_t)sizeof(*request) ? 0 : -1;
}
