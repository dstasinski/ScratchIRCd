# Multiplayer card tables (initial implementation)

ScratchIRCd supports dealer-controlled, in-memory card tables using `/CARDS`.
A channel member creates the table and becomes its dealer. IRC operators and
network administrators may override dealer-only operations. Other players
may request cards, inspect their own hands, play, discard, and ask for ranks.

## Commands

| Command | Action |
| --- | --- |
| `/CARDS NEW #Main [1-6] [JOKERS]` | Create 1-6 decks, optionally with two jokers per deck |
| `/CARDS REQUEST #Main 2` | Privately request two cards from the dealer; does not draw |
| `/CARDS ASK #Main Alice 7` | Publicly ask Alice for rank 7 (Go Fish); does not transfer |
| `/CARDS DEAL #Main Alice 5` | Dealer distributes five cards privately to Alice |
| `/CARDS SHOW #Main 3` | Dealer deals three public cards from the deck |
| `/CARDS BURN #Main 1` | Dealer removes one card unseen |
| `/CARDS TABLE #Main` | View public cards played or shown |
| `/CARDS GIVE #Main Bob AS` | Transfer a card privately to Bob (ownership checked) |
| `/CARDS REVEAL #Main` | Reveal your hand publicly without giving it up |
| `/CARDS MELD #Main 7H 7D 7C` | Move 2-6 owned cards face up (no rules validation) |
| `/CARDS DISCARDS #Main` | Inspect top discard and pile count |
| `/CARDS TAKE #Main` | Take the top discarded card into your hand |
| `/CARDS HAND #Main` | Privately inspect your own cards |
| `/CARDS PLAY #Main AS` | Publicly play a card from your hand |
| `/CARDS DISCARD #Main AS` | Discard a card privately; channel sees only the count |
| `/CARDS LEFT #Main` | View deck count and summary |
| `/CARDS STATUS #Main` | View deck count and summary |
| `/CARDS SHUFFLE #Main` | Dealer resets all hands and reshuffles |
| `/CARDS END #Main` | Dealer closes the table |

Card codes consist of rank `A23456789TJQK` followed by suit `SHDC`.
For example, `AS` is ace of spades and `TC` is ten of clubs.
With multiple decks, duplicate codes are allowed; each physical card is
tracked separately by the server.

## Privacy and lifetime

Every message is a 761 RPL_KEYVALUE, addressed individually to its recipient.
Dealer requests and hands are private; deals, plays, and discard counts are
announced to the channel. All recipients of a public event receive the same
event. The dealer does not automatically see another player's hand.

Tables are kept only in memory and are destroyed when their channel is removed
or the server shuts down. A disconnected dealer cannot authorize further deals
unless an IRC operator or administrator intervenes. Cards in a disconnected
player's hand remain allocated until shuffle or end.

This is a generic card-handling service, not a game rules engine. ASK does not
verify that a player holds the requested rank; players enforce game rules.

## Initial limits and future extensions

At most 32 concurrent tables, six decks per table, 64 player hands per table,
100 cards per DEAL, and no jokers in this first version. Card randomness uses
a lightweight noncryptographic generator.

Melds, discard-pile pickup,
jokers, dealer reassignment, and per-game rules are not yet implemented.

## Jokers and discards

`JOKERS` adds two jokers per deck, identified as `JK`. Physical duplicates are tracked independently. `DISCARD` creates an ordered pile. `DISCARDS` shows its top card, and `TAKE` moves the top card into the requesting player's hand. `MELD` checks ownership atomically but does not enforce game-specific meld rules.
