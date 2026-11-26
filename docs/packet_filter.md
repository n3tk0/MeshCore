# Packet filter and block list

The repeater, the room server and the companion (Faketec UI build) have an optional traffic
filter and a block list. **Everything is off by default**: with the filter off the firmware
behaves exactly as before, and nothing is written to flash.

Much of the filter is ported from [Dutch-MeshCore](https://github.com/Dutch-MeshCore/MeshCore)
(`dmc-dev`, MIT): the advert window, the path block, the sender and text rules, the malformed
scan, the age limit and the statistics. The block list, the ingress limit and the new-key quota
are new in this fork. The shared code is header-only, in `src/helpers/filter/`.

Build switch: `-D NO_PACKET_FILTER` removes the filter from a build (repeater, room server and
companion). It is set for all STM32 boards (`[stm32_base]`): with 256 KB of flash the filter does not fit.

## Where it works

| Node | What it judges | Result |
|---|---|---|
| Repeater, room server | flood packets this node would repeat | the packet is not repeated |
| Companion | flood packets it receives | drop (junk) or hide (your own choice) |

- The repeater / room server filter is the **last** check in `allowPacketForward`. Direct
  (routed) packets and a filter that is off always pass.
- A blocked key never reaches the node's own logic either (see the block list below).
- `filter dryrun on` counts everything the filter would do but lets all packets through. It
  applies to every check, including the block list.

## Commands

Repeater and room server: `filter ...` over serial, or remotely by an admin. The companion has
a subset (see the end of this page). Every command prints a short reply (under 158 characters).

| Command | Meaning |
|---|---|
| `filter` | state and counters |
| `filter on` / `off` | switch the filter |
| `filter reset` | all settings back to the defaults (off) |
| `filter dryrun [on\|off]` | count only |
| `filter hash <1-3>` | minimal path hash size (1 = off); smaller is dropped |
| `filter advert <hours>` | one flood advert per origin and window, 0-720 hours (0 = off); `filter advert clear` empties the cache |
| `filter path list [page]\|add\|remove <hex>` | drop flood packets whose path holds this repeater prefix (2-8 hex digits, 8 entries) |
| `filter channel list [page]\|add\|remove <#name\|Public>` | drop group texts of a channel (16 entries); a group text is dropped only when it decrypts with that channel's own key, not by the 1-byte channel hash alone (`Public` in any case) |
| `filter watch list [page]\|add\|remove <#name>` | channels (besides Public) that are decrypted for the checks below (4 entries) |
| `filter malformed [on\|off]` | drop group texts with a bad timestamp, empty text or invalid UTF-8 (Public only) |
| `filter age <minutes\|off>` | drop group texts older or newer than this (1-10080) |
| `filter sender list [page]\|add\|remove <name> [secs] [prob]` | rules on the sender name; `secs` 0 = block, else one per `secs`; `prob` 1-100; a name ending in `*` is a prefix (8 entries, 15 characters) |
| `filter text list [page]\|add\|remove <text> [secs] [prob]` | the same on the message text; a leading `^` anchors it at the start (8 entries, 23 characters) |
| `filter block list [page] [full]\|add\|remove\|clear <hex>` | the block list |
| `filter ingress <n> [secs] [soft]` / `off` | flood limit per entry repeater |
| `filter newkey <hours> [mins]` / `off` / `clear` | quota for new keys |
| `filter stats <topic>` | topics: `hash path advert channel malformed age sender text block ingress newkey air top reset` (the companion has `malformed age path sender text block`) |

Sender and text rules, `malformed` and `age` need the plaintext of a group text, so the node
decrypts the packet once. This works for **Public** and for channels added with `filter watch`.
A private channel that is not watched cannot be judged by content.

### Paged lists

A reply is at most 158 characters. The lists that can outgrow it (`filter block|channel|watch|sender|text|path list`)
are paged like the block list: `list [page]` (page 0 when left out) shows as many entries as surely fit (block 6,
channel and watch 4, sender and text 3, path 8) and, when there are more entries than a page holds, ends in
` [<shown>/<total> p<page>]` (`<shown>` counts up to and including this page; no comma inside, a comma separates the
entries). A page past the end says `> Filter: no page N (T entries)`. A reply that still would not fit ends in `..`.
The Android app walks every page for the card and for a repeater backup; a page that does not come, or comes cut, makes
the saved list **incomplete** and the app says so instead of saving it as whole.

### Block list

`filter block add <hex>`: a public key prefix of 8 to 64 hex digits (4 to 32 bytes), up to 32
entries, kept in flash. An entry matches every key that starts with it, so a short entry blocks a
family of keys: use long entries (a whole key is best).

A blocked key (repeater and room server):

- its flood ADVERT and ANON_REQ are not repeated;
- it gets no login (ANON_REQ is ignored), no neighbour entry and no ACL entry;
- a room server refuses its posts and does not push posts to it;
- a repeater mailbox does not store or deliver anything for it, and `mailbox add` refuses it.

`filter block list [page]` shows six entries a page: the first 8 bytes of each key, with a `+` when the entry is
longer, and ` [<shown>/<total> p<page>]` at the end. `filter block list <page> full` prints the whole keys, two a
page (the app uses it for a backup). `filter block remove <hex>` takes the whole entry or, when it is the only one
starting with it, the printed prefix (8 or more digits). `filter stats block` shows the hits per entry.
In a dry run `filter stats air` says "would save": nothing was really dropped.
The companion's "Block" in the UI refuses with "List is full" when the list is full.

A blocked key already in a repeater's or room server's ACL keeps its entry but gets nothing from it: no request or
command (so a blocked old admin cannot run `filter off`), no post, no path update, no answer.

### Ingress limit

`filter ingress <n> [secs] [soft]`: a flood packet's path lists the repeaters it went through;
the first entry is the repeater that took it from its originator. A node that spams the mesh is
always seen behind the same first entry. This allows `n` flood packets per `secs` seconds (default
60) for each such entry; a packet heard directly from its originator (empty path) counts under the
key "direct". With `soft` the limit is not a cliff: between `soft` and `n` the chance of being repeated falls
linearly to zero, above `n` nothing is repeated. The table is in RAM only (32 keys, least recently used replaced).
`filter stats ingress` lists the keys with most drops.

### New-key quota

`filter newkey <hours> [mins]`: the node remembers when it first saw each key (its first 4 bytes)
in an advert or an anonymous request, 256 keys (64 on STM32), in the file `/filter_seen`. A key seen
for the first time less than `hours` ago is "new". A new key's flood adverts are repeated at most
once per `mins` minutes (default 60), its anonymous requests at most once a minute. Keys already in
the node's ACL, contacts or mailbox are never new. Nothing is recorded while the clock is not set.

Three limits apply to a new key: its own quota (above); a quota for the whole class of new keys (30 packets a minute
in all, `NEWKEY_CLASS_LIMIT`), so a flood of one-time keys, each new and so each inside its own quota, still cannot
pass; and, once the table is full, one new key admitted by eviction a minute, so two keys cannot push each other out of the
table and both pass every time. A check that only looks (a packet another check drops afterwards) changes nothing:
the table, the quotas and the ingress limit are only charged for a packet that is really repeated.

### Clock

The advert window counts seconds of an uptime counter built from `millis()`. The repeater and room server main loop
feed it every pass (`PacketFilter::tick()`), so it sees every 49.7 day wrap of `millis()` even when no packet arrives.

## Flash use

- Settings are in `/filter_prefs` (companion: `/cfilter`), written only when the settings really
  changed (a signature of what would be written is compared), through a temporary file renamed
  over the old one. A reset during a write keeps the old file. Counters are never saved.
- `/filter_seen` is written at most once an hour and only when it changed; a failed write is retried
  an hour later. Nothing is written while the battery is low.

## What the filter cannot do

- **A direct message cannot be blocked on the way.** A direct message carries only a 1-byte hash of
  its sender and of its recipient, and is encrypted for the recipient. Only the receiver can know
  who sent it. The companion therefore drops DMs from a blocked contact when they arrive (they are
  still acknowledged silently, so the sender does not learn it), but no repeater can do this.
- **A group text carries no key or name outside the encryption.** A repeater can judge the content
  only of channels it holds the key of (Public, and `filter watch`).
- **The path hash size is chosen by the originator** (1 to 3 bytes). `filter hash` can only refuse
  packets with short hashes, and a path entry is not a unique identity: a short prefix can match
  several repeaters.
- The ingress key is the first path entry as the originator's neighbour wrote it; a spammer who
  sends from a different place gets a different key.
- A node that is not repeating (companion without `repeat`) cannot stop others from hearing a packet.

## Companion

The companion judges what it receives. Verdicts:

- **drop**: junk (malformed, age, blocked repeater path, advert of a blocked key). The packet is
  neither shown nor repeated.
- **hide**: your own choice (sender or text rule, a muted channel). The packet is not shown in the
  app or in the UI, but it is still repeated when repeat mode is on.

Block list on the companion:

- adverts and anonymous requests of a blocked key do not reach the app, do not add or update contacts and are not
  shown in Tools > Nearby (neither in the app's discovery answers nor in the UI's list); nothing from it is forwarded
  when repeat is on, and neither is anything routed through a blocked repeater path;
- "Block" in the UI **keeps the contact stored but hides it**: it is not sent to the phone app (contact list, new
  contacts, by-key and export), not in the UI's contacts, recipients and trace lists. Its DMs are still decrypted and
  acknowledged as normal, then dropped (not shown, not queued to the app); it gets no telemetry or other request answer.
  An advert of the key does not update the hidden contact. Removing the key from the list shows the contact again.
  The hiding follows the filter: with the filter off (or in a dry run) nothing is blocked, so the contact is visible;
- a muted sender, text rule, malformed / age check or a hidden channel also applies to a zero-hop **direct** group
  text (the mesh only offers flood packets to the filter, so these are judged where the text is delivered); group data
  of a hidden channel is not handed to the app.

Command subset (through the normal CLI command path): `filter`, `on`, `off`, `reset`, `dryrun`,
`malformed`, `age`, `path`, `sender`, `text`, `block`, `stats <malformed|age|path|sender|text|block>`.
Settings are saved in `/cfilter`.

### Faketec UI

All texts are in Bulgarian and English and follow the language setting.

- Hold a contact, **Block** (Блокирай): asks for confirmation, then blocks the key, removes the
  contact and deletes its stored data.
- Hold a channel message, **Mute sender** (Заглуши подател): adds a sender rule on that name.
  **Mute channel** (Заглуши канал): hides that channel on this device. (This is separate from the
  existing channel Mute, which only silences alerts.)
- Settings > Messages > **Blocked** (Блокирани): blocked keys (first 4 bytes), muted senders (`@`)
  and muted channels (`#`). Press an entry and confirm to remove it.

Blocking switches the filter on. `filter off` turns all of it off again without losing the lists.

## Limits

| Item | Limit |
|---|---|
| block list | 32 entries, 4-32 bytes each |
| path block | 8 entries, 1-4 bytes |
| sender / text rules | 8 + 8 |
| blocked channels / watched channels | 16 / 4 |
| ingress table | 32 keys (RAM) |
| new keys | 256 (64 on STM32) |
| muted channels (companion) | 4 |

## Tests

Native unit tests: `pio test -e native` (`test_packet_filter`, `test_companion_filter`,
`test_filter_rules`, `test_filter_policy`, `test_filterstats`, `test_soft_limiter`). The screens
of the UI can be reviewed with `sh tools/ui-sim/build.sh`.
