/**
 * @file mail.c
 * @brief Detached sendmail-compatible delivery for NickServ recovery mail.
 */

#include "mail.h"

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
 * A detached child inherits every listener/client/resolver descriptor owned by
 * the IRC process. Close all non-standard descriptors before starting mail
 * delivery so the helper cannot keep IRC sockets alive across shutdown or
 * RESTART. The helper creates its own sendmail stdin pipe afterwards.
 */
static void close_inherited_descriptors(void) {
    long maximum = sysconf(_SC_OPEN_MAX);
    int fd;

    if (maximum < 0L || maximum > 65536L) maximum = 65536L;
    for (fd = 3; fd < (int)maximum; ++fd) close(fd);
}

static void deliver_one(const char *sendmail_path, const MailRequest *request) {
    int input_pipe[2] = {-1, -1};
    pid_t child;
    char message[IRCD_MAIL_BODY_MAX + IRCD_EMAIL_MAX * 2U +
                 IRCD_MAIL_SUBJECT_MAX + 256U];
    int length;

    if (sendmail_path == NULL || *sendmail_path == '\0' || request == NULL) return;
    if (pipe(input_pipe) != 0) return;

    child = fork();
    if (child == 0) {
        (void)dup2(input_pipe[0], STDIN_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        execl(sendmail_path, sendmail_path, "-t", "-i", (char *)NULL);
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

int mail_send_async(const char *sendmail_path, const MailRequest *request) {
    pid_t intermediate;
    pid_t waited;
    int status = 0;

    if (sendmail_path == NULL || *sendmail_path == '\0' || request == NULL) return -1;

    intermediate = fork();
    if (intermediate < 0) return -1;
    if (intermediate == 0) {
        pid_t worker = fork();
        if (worker < 0) _exit(1);
        if (worker > 0) _exit(0);
        close_inherited_descriptors();
        deliver_one(sendmail_path, request);
        _exit(0);
    }

    do {
        waited = waitpid(intermediate, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != intermediate) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
