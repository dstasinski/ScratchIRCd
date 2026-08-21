#ifndef IRCD_MAIL_H
#define IRCD_MAIL_H

/**
 * @file mail.h
 * @brief Asynchronous outbound mail queue used by NickServ recovery.
 *
 * ScratchIRCd never invokes a shell for mail delivery. A dedicated worker
 * thread executes the configured sendmail-compatible binary directly with
 * `-t -i` and feeds one fully formed RFC 5322-style message over stdin.
 * This keeps potentially slow local-MTA delivery out of the IRC event loop.
 */

#include <pthread.h>
#include "config.h"

typedef struct MailRequest {
    char to[IRCD_EMAIL_MAX + 1U];
    char from[IRCD_EMAIL_MAX + 1U];
    char subject[IRCD_MAIL_SUBJECT_MAX + 1U];
    char body[IRCD_MAIL_BODY_MAX + 1U];
} MailRequest;

typedef struct MailSender {
    pthread_t thread;
    int request_read_fd;
    int request_write_fd;
    int running;
    char sendmail_path[IRCD_CONFIG_PATH_MAX + 1U];
} MailSender;

int mail_sender_init(MailSender *sender, const char *sendmail_path);
void mail_sender_destroy(MailSender *sender);
int mail_sender_submit(MailSender *sender, const MailRequest *request);

#endif /* IRCD_MAIL_H */
