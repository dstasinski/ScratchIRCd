# Configurable command aliases

Add blocks to the existing key=value `ircd.conf` file:

```ini
alias blah {
    target = blah
}

alias Trivia {
    target = TriviaBot
}
```

A client sending `BLAH something with spaces` is handled as a
`PRIVMSG blah :something with spaces` from that client. A client sending
`TRIVIA start` messages `TriviaBot`. Most IRC clients expose these
as `/blah something with spaces` and `/Trivia start`.

Alias names are case-insensitive, begin with an ASCII letter, and contain
only ASCII letters, digits, or underscore. Targets must be valid IRC
nicknames. Up to 64 aliases may be defined. Duplicates, empty targets,
and malformed blocks make configuration loading fail. One target per block.

Built-in IRC commands take precedence over aliases, including NICKSERV,
CHANSERV and MEMOSERV. An alias cannot replace a built-in command.
Aliases do not expand into other aliases or execute arbitrary commands.

Aliases use the normal PRIVMSG handler and its permissions, routing,
message-length checks, and errors. If the target nickname is not connected, the server returns numeric
440 (ERR_SERVICESDOWN) naming that target instead of forwarding the message.
Other PRIVMSG errors and restrictions remain unchanged. Alias definitions reload on a successful
`/REHASH`; a rejected REHASH leaves the active aliases unchanged.
