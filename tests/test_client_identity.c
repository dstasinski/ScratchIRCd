/**
 * @file test_client_identity.c
 * @brief Unit tests for ScratchIRCd's three-field client identity model.
 */

#include "client.h"

#include <assert.h>
#include <string.h>

int main(void) {
    Client *client = client_create(-1, 1U, 0, "203.0.113.42");

    assert(client != NULL);

    /* A new connection begins with its real IP as the visible fallback. */
    assert(strcmp(client->real_ip, "203.0.113.42") == 0);
    assert(client->real_host[0] == '\0');
    assert(strcmp(client->display_host, "203.0.113.42") == 0);

    /* A verified real hostname is independent of the public display value. */
    strcpy(client->real_host, "customer.example.net");
    strcpy(client->display_host, client->real_host);
    assert(strcmp(client->real_ip, "203.0.113.42") == 0);
    assert(strcmp(client->real_host, "customer.example.net") == 0);
    assert(strcmp(client->display_host, "customer.example.net") == 0);

    /* Cloaks/vhosts alter display_host only; security identity is preserved. */
    strcpy(client->display_host, "staff.example.net");
    assert(strcmp(client->real_ip, "203.0.113.42") == 0);
    assert(strcmp(client->real_host, "customer.example.net") == 0);
    assert(strcmp(client->display_host, "staff.example.net") == 0);

    client_free(client);
    return 0;
}
