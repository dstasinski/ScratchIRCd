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

## Limits and future extensions

At most 32 concurrent tables, six decks per table, 64 player hands per table,
and 100 cards per DEAL. Jokers are optional (two per deck). Card randomness
currently uses a lightweight noncryptographic generator, so this system is
not intended for gambling or high-stakes competitive play.

The current MELD operation displays cards publicly but does not maintain
independent named meld groups. The current TAKE operation retrieves only the
top discard, not the entire pile. No built-in turn order, scoring, betting,
teams, rule validation, player reconnection, or game-specific victory logic
is implemented. SHUFFLE resets all hands and piles rather than reshuffling
only the undrawn cards.

## Jokers and discards

`JOKERS` adds two jokers per deck, identified as `JK`. Physical duplicates are tracked independently. `DISCARD` creates an ordered pile. `DISCARDS` shows its top card, and `TAKE` moves the top card into the requesting player's hand. `MELD` checks ownership atomically but does not enforce game-specific meld rules.


## Game possibilities

The games below are **possibilities for a shared card table**, not claims
that ScratchIRCd implements each game's rules. Players and a human dealer
must enforce turn order, scoring, legal moves, and winners. Commands marked
"future" are not currently available.

### Games using one standard deck

| Game | Typical players | How it could work | Limitations / future improvements |
| --- | --- | --- | --- |
| Five-Card Draw Poker | 2-6 | DEAL five cards each; DISCARD unwanted cards; dealer DEALs replacements; REVEAL at showdown | Betting, turns, and hand ranking are manual; REVEAL does not surrender cards |
| Blackjack (21) | 1+ against dealer | DEAL private hands, SHOW dealer cards, REQUEST additional hits, REVEAL at resolution | No HIT/STAND scoring commands; a hidden dealer hole card must be held in the dealer's hand |
| Texas Hold'em | 2-10 | DEAL two private cards each; BURN and SHOW flop, turn, river; REVEAL at showdown | Betting, side pots, and ranking are manual; TABLE currently combines all publicly played/shown cards |
| Hearts | 4 | DEAL thirteen cards each, GIVE pass cards, PLAY tricks | No trick ownership, turn enforcement, or score tracking |
| Spades | 4 | DEAL thirteen cards each, PLAY tricks, manually track bids and teams | No bidding, tricks, or team scoring built in |
| Bridge | 4 | DEAL thirteen cards each, PLAY tricks; REVEAL can expose dummy's hand | No auction, contract, dummy control, or trick accounting |
| Gin Rummy | 2 | DEAL hands, DISCARD and TAKE the top discard, MELD sets | No knock/deadwood scoring; MELD has no game-rule validation |
| Go Fish | 2-6 | ASK another player for a rank, GIVE matching cards, REQUEST the dealer to draw, MELD completed books | ASK does not automatically inspect or transfer cards; player cooperation is required |
| Crazy Eights | 2-7 | DEAL hands, PLAY matching cards, DISCARD and TAKE as agreed | TAKE retrieves the discard, not a new deck card; use REQUEST/DEAL for draws; no suit declaration or rule validation |
| Old Maid | 2-6 | DEAL a specially prepared deck, GIVE cards and MELD matching pairs | Needs a custom deck with a removed card or a designated unmatched card; random concealed transfer is future |
| War | 2 | DEAL equal hands, PLAY cards to compare ranks | Won-card piles, draw order, and returning won cards to a player are future; current hands do not act as ordered personal decks |

### Games using multiple decks

| Game | Typical configuration | How it could work | Limitations / future improvements |
| --- | --- | --- | --- |
| Multi-deck Blackjack | 6 or 8 standard decks (currently maximum 6) | NEW #Main 6, DEAL player hands, SHOW dealer cards, BURN as needed | Eight decks require increasing the current limit; no scoring |
| Canasta | 2 decks plus 4 jokers; commonly 4 players | NEW #Main 2 JOKERS, DEAL hands, MELD sets, DISCARD and TAKE | Needs named melds, wild-card rules, and whole-discard-pile pickup |
| Hand and Foot | Often 5 decks plus jokers; commonly 4 players | NEW #Main 5 JOKERS, DEAL large hands, MELD, DISCARD | Needs separate hand/foot ownership, named melds, and pile pickup |
| Double-Deck Pinochle | 80-card specialized deck; commonly 4 players | Play tricks and melds with multiple copies of each rank/suit | NEW currently builds full 52-card decks; needs custom rank-filtered decks (A,T,K,Q,J with four copies per suit) |

### Suggested future generic operations

- **Named melds:** preserve distinct public meld groups and their owners,
  allow adding cards to an existing meld, and inspect each meld separately.
- **Whole discard-pile pickup:** take the complete discard pile in one
  atomic operation when the game's rules allow it.
- **Multiple hands/piles per player:** support Hand and Foot, War, and
  other games that distinguish active hands from reserves or won piles.
- **Custom deck composition:** select ranks, suits, omitted cards,
  joker counts, and larger deck limits for Pinochle and other variants.
- **Random concealed transfer:** support Old Maid without revealing
  the selected card to the requesting player in advance.
- **Trick and turn support:** group played cards into tricks, assign
  trick winners, optionally manage player turns and teams.
- **Optional scoring and rule modules:** keep the generic card-table
  service independent from individual games while allowing future
  rules-aware extensions.

### Sample command sequences

**Five-Card Draw Poker:**

```irc
/CARDS NEW #Main
/CARDS DEAL #Main Alice 5
/CARDS DEAL #Main Bob 5
/CARDS HAND #Main
/CARDS DISCARD #Main 7H
/CARDS REQUEST #Main 1
/CARDS DEAL #Main Alice 1
/CARDS REVEAL #Main
```

**Texas Hold'em (dealer controls community cards):**

```irc
/CARDS NEW #Main
/CARDS DEAL #Main Alice 2
/CARDS DEAL #Main Bob 2
/CARDS BURN #Main 1
/CARDS SHOW #Main 3
/CARDS BURN #Main 1
/CARDS SHOW #Main 1
/CARDS BURN #Main 1
/CARDS SHOW #Main 1
/CARDS TABLE #Main
```

**Go Fish (the ASK command is intentionally retained):**

```irc
/CARDS NEW #Main
/CARDS DEAL #Main Alice 7
/CARDS DEAL #Main Bob 7
/CARDS ASK #Main Bob 7
/CARDS GIVE #Main Alice 7H
/CARDS REQUEST #Main 1
```

**Canasta (partial workflow, not full rules):**

```irc
/CARDS NEW #Main 2 JOKERS
/CARDS DEAL #Main Alice 11
/CARDS DEAL #Main Bob 11
/CARDS MELD #Main 7H 7D 7C
/CARDS DISCARD #Main AS
/CARDS DISCARDS #Main
/CARDS TAKE #Main
```

The card codes in these examples are illustrative; PLAY, GIVE, DISCARD,
and MELD only succeed when the player actually holds those cards.
