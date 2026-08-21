#ifndef IRCD_MAIL_H
#define IRCD_MAIL_H

/**
 * @file mail.h
 * @brief Detached outbound mail delivery used by NickServ recovery.
 *
 * Delivery uses a configured sendmail-compatible binary without invoking a
 * shell. A double-fork helper keeps potentially slow MTA work out of the IRC
 * event loop and avoids leaving zombie children behind.
 */

#include "config.h"

typedef struct MailRequest {
    char to[IRCD_EMAIL_MAX + 1U];
    char from[IRCD_EMAIL_MAX + 1U];
    char subject[IRCD_MAIL_SUBJECT_MAX + 1U];
    char body[IRCD_MAIL_BODY_MAX + 1U];
} MailRequest;

int mail_send_async(const char *sendmail_path, const MailRequest *request);

#endif /* IRCD_MAIL_H */
