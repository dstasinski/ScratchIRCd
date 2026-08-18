/**
 * @file test_visibility.c
 * @brief Unit tests for shared LIST/NAMES/WHO/WHOIS visibility rules.
 */

#include "visibility.h"

#include <assert.h>
#include <string.h>

int main(void) {
    Client requester = {0};
    Client subject = {0};
    Channel public_channel = {0};
    Channel secret_channel = {0};
    Channel local_channel = {0};
    ChannelMember public_member = {0};
    ClientChannelLink requester_link = {0};

    requester.registered = 1;
    subject.registered = 1;
    strcpy(public_channel.name, "#public");
    strcpy(secret_channel.name, "#secret");
    strcpy(local_channel.name, "&local");

    secret_channel.modes = CHANNEL_MODE_SECRET;

    assert(visibility_list_channel(&requester, &public_channel));
    assert(!visibility_list_channel(&requester, &secret_channel));
    assert(!visibility_list_channel(&requester, &local_channel));

    subject.modes = CLIENT_MODE_INVISIBLE;
    assert(!visibility_who_user(&requester, &subject));

    public_member.client = &subject;
    public_channel.members = &public_member;
    public_channel.member_count = 1U;
    requester_link.channel = &public_channel;
    requester.channels = &requester_link;
    requester.channel_count = 1U;

    assert(visibility_share_channel(&requester, &subject));
    assert(visibility_who_user(&requester, &subject));

    subject.modes = CLIENT_MODE_PRIVATE;
    assert(!visibility_whois_channel(&requester, &subject, &secret_channel));
    assert(visibility_whois_channel(&subject, &subject, &secret_channel));

    requester.modes = CLIENT_MODE_OPER;
    assert(visibility_is_oper(&requester));
    assert(visibility_list_channel(&requester, &secret_channel));
    assert(visibility_who_user(&requester, &subject));

    return 0;
}
