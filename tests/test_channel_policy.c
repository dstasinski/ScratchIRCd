#include "channel_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Client client;
    Channel channel;

    memset(&client, 0, sizeof(client));
    memset(&channel, 0, sizeof(channel));

    client.id = 42U;
    snprintf(client.nick, sizeof(client.nick), "%s", "Daniel");
    snprintf(client.user, sizeof(client.user), "%s", "daniel");
    snprintf(client.host, sizeof(client.host), "%s", "Example.COM");
    snprintf(client.ip, sizeof(client.ip), "%s", "192.0.2.10");

    assert(irc_mask_match("d?niel!*@example.*", "Daniel!daniel@Example.COM"));
    assert(irc_mask_match("[Test]", "{test}"));
    assert(!irc_mask_match("other!*@*", "Daniel!daniel@Example.COM"));

    assert(channel_mask_add(&channel.ban_list, "Daniel!*@*") == 0);
    assert(channel_client_is_banned(&channel, &client));
    assert(channel_mask_add(&channel.exception_list, "*!daniel@example.com") == 0);
    assert(!channel_client_is_banned(&channel, &client));

    assert(!channel_invite_has(&channel, client.id));
    assert(channel_invite_add(&channel, client.id) == 0);
    assert(channel_invite_has(&channel, client.id));
    assert(channel_invite_consume(&channel, client.id));
    assert(!channel_invite_has(&channel, client.id));

    channel.join_throttle_count = 2U;
    channel.join_throttle_seconds = 60U;
    assert(channel_join_throttle_allows(&channel, client.id));
    channel_join_throttle_record(&channel, client.id);
    assert(channel_join_throttle_allows(&channel, client.id));
    channel_join_throttle_record(&channel, client.id);
    assert(!channel_join_throttle_allows(&channel, client.id));

    channel_mask_clear(&channel.ban_list);
    channel_mask_clear(&channel.exception_list);
    channel_invite_clear(&channel);
    channel_join_throttle_clear(&channel);

    puts("channel policy tests passed");
    return 0;
}
