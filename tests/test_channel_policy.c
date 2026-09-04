#include "channel_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Client client;
    Channel channel;
    unsigned int i;

    memset(&client, 0, sizeof(client));
    memset(&channel, 0, sizeof(channel));

    client.id = 42U;
    snprintf(client.nick, sizeof(client.nick), "%s", "Daniel");
    snprintf(client.user, sizeof(client.user), "%s", "daniel");
    snprintf(client.real_ip, sizeof(client.real_ip), "%s", "192.0.2.10");
    snprintf(client.real_host, sizeof(client.real_host), "%s", "real.example.net");
    snprintf(client.display_host, sizeof(client.display_host), "%s", "Example.COM");

    assert(irc_mask_match("d?niel!*@example.*", "Daniel!daniel@Example.COM"));
    assert(irc_mask_match("[Test]", "{test}"));
    assert(!irc_mask_match("other!*@*", "Daniel!daniel@Example.COM"));

    /* Channel masks operate only on nick!user@display_host. */
    assert(channel_mask_add(&channel.ban_list, "Daniel!*@example.com") == 0);
    assert(channel_client_is_banned(&channel, &client));
    assert(!channel_client_is_banned_protected(&channel, &client));

    /* A ban explicitly authorized by +a/+q is effective for protected users. */
    assert(channel_mask_add_authorized(&channel.ban_list,
                                       "Daniel!*@example.com", 1) == 0);
    assert(channel_client_is_banned_protected(&channel, &client));

    assert(channel_mask_add(&channel.exception_list, "*!daniel@example.com") == 0);
    assert(!channel_client_is_banned(&channel, &client));
    assert(!channel_client_is_banned_protected(&channel, &client));

    channel_mask_clear(&channel.ban_list);
    channel_mask_clear(&channel.exception_list);
    assert(channel_mask_add(&channel.ban_list, "*!*@real.example.net") == 0);
    assert(!channel_client_is_banned(&channel, &client));
    assert(channel_mask_add(&channel.ban_list, "*!*@192.0.2.10") == 0);
    assert(!channel_client_is_banned(&channel, &client));

    assert(!channel_invite_has(&channel, client.id));
    assert(channel_invite_add(&channel, client.id) == 0);
    assert(channel_invite_has(&channel, client.id));
    assert(channel_invite_consume(&channel, client.id));
    assert(!channel_invite_has(&channel, client.id));

    /* +j is channel-wide: different client IDs share one join window. */
    channel.join_throttle_count = 2U;
    channel.join_throttle_seconds = 60U;
    assert(channel_join_throttle_allows(&channel, 42U));
    channel_join_throttle_record(&channel, 42U);
    assert(channel_join_throttle_allows(&channel, 99U));
    channel_join_throttle_record(&channel, 99U);
    assert(!channel_join_throttle_allows(&channel, 42U));
    assert(!channel_join_throttle_allows(&channel, 100U));

    /* Clearing/reconfiguring +j starts a fresh window. */
    channel_join_throttle_clear(&channel);
    assert(channel_join_throttle_allows(&channel, 100U));
    assert(channel.join_throttle_window_count == 0U);
    assert(channel.join_throttle_window_start == 0);

    /* ChanServ status is tracked separately from ordinary MODE grants. */
    assert(channel_add_client(&channel, &client) == 0);
    assert(channel_add_privileges(&channel, &client, CHANNEL_PRIV_VOICE) == 0);
    assert(channel_set_service_privileges(&channel, &client,
                                          CHANNEL_PRIV_OPERATOR) == 0);
    assert(channel_find_member(&channel, &client)->privileges ==
           (CHANNEL_PRIV_VOICE | CHANNEL_PRIV_OPERATOR));
    assert(channel_set_service_privileges(&channel, &client, 0U) == 0);
    assert(channel_find_member(&channel, &client)->privileges == CHANNEL_PRIV_VOICE);
    assert(channel_set_service_privileges(&channel, &client,
                                          CHANNEL_PRIV_OPERATOR) == 0);
    channel_forget_service_privileges(&channel);
    assert(channel_find_member(&channel, &client)->service_privileges == 0U);
    assert(channel_set_service_privileges(&channel, &client, 0U) == 0);
    assert(channel_find_member(&channel, &client)->privileges ==
           (CHANNEL_PRIV_VOICE | CHANNEL_PRIV_OPERATOR));
    channel_remove_client(&channel, &client);

    /* Attacker-controlled linked lists have hard cardinality bounds. */
    channel_mask_clear(&channel.ban_list);
    for (i = 0U; i < IRC_CHANNEL_MASK_LIST_MAX; ++i) {
        char mask[64];
        (void)snprintf(mask, sizeof(mask), "*!user%u@host.example", i);
        assert(channel_mask_add(&channel.ban_list, mask) == 0);
    }
    assert(channel_mask_add(&channel.ban_list, "*!overflow@host.example") == -2);
    /* A duplicate remains harmless and succeeds even when the list is full. */
    assert(channel_mask_add(&channel.ban_list, "*!user0@host.example") == 0);

    channel_invite_clear(&channel);
    for (i = 0U; i < IRC_CHANNEL_INVITE_MAX; ++i)
        assert(channel_invite_add(&channel, (uint64_t)i + 1U) == 0);
    assert(channel_invite_add(&channel, UINT64_C(999999)) == -2);
    assert(channel_invite_add(&channel, 1U) == 0);

    channel_mask_clear(&channel.ban_list);
    channel_mask_clear(&channel.exception_list);
    channel_mask_clear(&channel.invite_exception_list);
    channel_invite_clear(&channel);
    channel_join_throttle_clear(&channel);

    puts("channel policy tests passed");
    return 0;
}
