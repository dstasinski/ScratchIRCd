#include "cloak.h"
#include "client.h"
#include "modes.h"
#include "runtime_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void setup(ServerConfig *config) {
    memset(config, 0, sizeof(*config));
    (void)snprintf(config->cloak_prefix, sizeof(config->cloak_prefix), "dru");
    (void)snprintf(config->cloak_key, sizeof(config->cloak_key),
                   "test-only-cloak-key-0123456789abcdef");
}

int main(void) {
    ServerConfig config;
    char a[IRC_HOST_MAX + 1U], b[IRC_HOST_MAX + 1U], c[IRC_HOST_MAX + 1U];
    char v61[IRC_HOST_MAX + 1U], v62[IRC_HOST_MAX + 1U];
    Client *client;

    setup(&config);

    assert(cloak_generate(&config, "203.0.113.42", "", a, sizeof(a)) == 0);
    assert(cloak_generate(&config, "203.0.113.42", "", b, sizeof(b)) == 0);
    assert(strcmp(a, b) == 0);
    assert(strstr(a, ".IP") != NULL);

    assert(cloak_generate(&config, "203.0.113.99", "", b, sizeof(b)) == 0);
    assert(cloak_generate(&config, "203.0.114.99", "", c, sizeof(c)) == 0);
    {
        char *a1 = strchr(a, '.');
        char *b1 = strchr(b, '.');
        char *c1 = strchr(c, '.');
        assert(a1 && b1 && c1);
        assert(strcmp(a1, b1) == 0);
        assert(strcmp(a1, c1) != 0);
        assert(strcmp(strchr(a1 + 1, '.'), strchr(c1 + 1, '.')) == 0);
    }

    assert(cloak_generate(&config, "2001:db8:1:2::1234", "", v61, sizeof(v61)) == 0);
    assert(cloak_generate(&config, "2001:0db8:0001:0002:0000:0000:0000:1234", "", v62, sizeof(v62)) == 0);
    assert(strcmp(v61, v62) == 0);

    assert(cloak_generate(&config, "203.0.113.42",
                          "c-73-45-12-99.ph.ph.cox.net", a, sizeof(a)) == 0);
    assert(strncmp(a, "dru-", 4U) == 0);
    assert(strstr(a, ".ph.ph.cox.net") != NULL);
    assert(strstr(a, "c-73-45-12-99") == NULL);

    client = client_create(-1, 1U, 0, "203.0.113.42");
    assert(client != NULL);
    client->modes = client_mode_add(client->modes, CLIENT_MODE_CLOAKED);
    cloak_refresh_display_host(&config, client);
    assert(strstr(client->display_host, ".IP") != NULL);
    (void)snprintf(client->display_host, sizeof(client->display_host), "staff.example");
    client->modes = client_mode_add(client->modes, CLIENT_MODE_VHOST);
    cloak_refresh_display_host(&config, client);
    assert(strcmp(client->display_host, "staff.example") == 0);
    client->modes = client_mode_remove(client->modes, CLIENT_MODE_VHOST);
    cloak_refresh_display_host(&config, client);
    assert(strstr(client->display_host, ".IP") != NULL);
    client_free(client);
    return 0;
}
