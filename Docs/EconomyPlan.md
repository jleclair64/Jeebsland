# Economy plan (2026-11-26, agent economy, Docs/ThursdayList.md item 32)

Owner: "I want all items to be able to be traded. Please set up a trading system and also a game-wide auction system. I want ingots to be on an
account basis, not a class basis ... I want a player to be able to put an item up for sale at a price they set, and other players can buy it at
that price from the game store ... maybe a different tab at the bank. Also, I want the bank to be on an account basis." And: "only buy wizard stuff
in Silver Point, and only buy knight stuff in Blackrock and Oak Hill. The general stores will sell all things that are not class specific."

Everything below is decided on the server; the client only asks (Server RPCs) and draws what the server sends back.

## 1. Account ingots and bank

Already true since the separate class packs (2026-11-24): there is one character file per account (`Saved/Characters/<key>.sav`), the ingots and the
bank are top-level fields of `UJeebSave`, and only the pack, the weapon bar, worn gear and the potion slot are kept per class (`FJeebClassPack`).
A class switch (`SwapPawnClass`) captures and re-applies the same file, so the knight, troll and wizard already see the same ingots and vault.
- No migration and no save-version bump: no save ever held a per-class bank or per-class ingots, so there is nothing to merge.
- Checked anyway: the `JEEBPACK applied` log line now also prints the ingots and the bank's item count and id sum, and `Tools/class_net.sh`
  (old-format save, then knight -> wizard -> troll -> knight -> wizard) must print the same numbers for every class.
- Dying still drops the ingots you carry into the sack (unchanged; the ingots are the account's, so a troll's death drops them too).

## 2. Player-to-player trade

- **Asking:** face another player within 3 m and press E ("Press E to trade with Name"), or with the cursor out (Tab) right-click their figure.
  The server tells them "Name wants to trade"; they accept by doing the same back within 30 s. Either side may be any class.
- **Window:** both players see a two-sided window (yours left, theirs right): up to 12 items from your pack and an ingot amount (typed).
  Click a pack item to offer the whole stack, click an offered item to take it back. Sized fish go as themselves (their length shows).
- **Two accepts:** ACCEPT (stage 1, the offer) then CONFIRM (stage 2, a summary of exactly what you give and get). Any change to either offer
  in stage 1 clears both accepts; in stage 2 the offers are locked (changes refused). Every accept carries the offer version the client saw; a
  stale one is refused.
- **The swap** (server, in one function, no waits): both still connected and within 6 m, every offered slot still holds exactly the item, size
  and at least the count that was offered, the ingots are still there, and each side's pack has room for what it gets after what it gives leaves
  (worked out on copies first). Then both packs and ingot counts are replaced at once and both characters are saved. Anything wrong: the trade
  is cancelled with the reason, nothing moves.
- **Cancelled by:** either side closing it, walking over 8 m apart, dying, logging out, a class switch, or travelling.
- **Log:** every completed trade goes to `Saved/Trades/trades_<port>.log` (time, both accounts, what each gave) and to the server log (`JEEBTRADE`).

## 3. The Market (game-wide)

A MARKET tab beside THE VAULT at every bank. Every world server on the host shares `Saved/Market/` (like Saved/Accounts and Saved/Boards).
- **Listings:** `Saved/Market/Live/<id>.json`, one file per listing (seller account and name, item id, count, size, price, time). Written to a
  side file and moved in, so readers never see half a file.
- **Selling:** at a bank, pick a pack item (the whole stack), type a price, LIST. The server takes the item out of your pack (escrow) and writes
  the listing, then saves your character. Limit **10 listings per account**. No listing fee.
- **Buying:** browse by category (All, Weapons, Armour, Materials, Fish, Food and potions, Arrows and rocks, Wizard, Troll, Other) or type a
  search (part of the item's name), cheapest first, 12 a page. BUY at the listed price: the server checks your ingots and room (pack, else bank),
  takes the ingots, then **claims the listing by moving its file** from Live to Done (`MoveFileW` without replace: of two servers racing for one
  listing exactly one move succeeds; the loser refunds its buyer and says "Someone bought that just before you"). Then the item goes to your
  pack (or bank) and the seller is paid.
- **Tax:** 5% of the price (rounded down) is kept by the market (an ingot sink); the seller gets the rest. Shown on the sell button.
- **Paying the seller (online or not, any world):** a mail file `Saved/Market/Mail/<key>/<id>.json`. Each world checks the mail of its logged-in
  players every 5 s (and at login): ingots go to the account, items (cancel returns that could not fit) to the bank. The applied mail id is kept in
  the character (`MailDone`, saved) before the file is deleted, so a crash between the two never pays twice. The player gets a message: "Market: your
  Iron Sword sold for 95 ingots (5 tax)."
- **Cancelling:** MY LISTINGS shows yours; CANCEL claims the file the same way and gives the item back (pack, else bank, else mail).
- No expiry for now.
- **Log:** `Saved/Market/Log/market_<port>.log` (list, buy, cancel, mail).

## 4. Shops by town

`JeebShops::TownAt(position)`: the nearest town centre (JeebWorld::Towns, plus the old village site for Blackrock) decides what a shop sells.
Item classes are `JeebItems::ItemClass` (0 everyone, 1 knight, 2 troll, 3 wizard), except that the plain wooden arrow counts as everyone's.
- **Oak Hill and Blackrock** (Blackrock has no shop at the moment): everyone's goods plus knight goods (swords, spears, bows, crossbows, shields,
  knight armour, arrows).
- **Silver Point:** the broom stall becomes the wizard's shop (same spot, a building later): brooms, wizard goods (wands, staffs, spell books,
  crystal balls, pipes, wizard hats, robes and boots) and everyone's goods. It now also buys your goods, like a trading post.
- **Steele Creek** (its smith is a troll): everyone's goods plus troll goods (clubs, broadswords, troll armour, throwing rocks). Chosen because
  Birch Grove, the troll town, is not built yet; move them there when it is.
- **Everywhere else** (none yet): everyone's goods only.
- The hourly stock roll keeps its old wares in the same order (indices unchanged) and adds wizard and troll wares at the end; each shop shows only
  what its town may sell, and the server refuses a Buy whose ware the player's town does not sell. The mystery satchel is unchanged.

## Checks
- Online, one world, server + two clients (`Tools/economy_net.sh`): a real trade both ways (items, a sized fish, ingots), and refused cheats: offering
  a slot you don't have, changing the offer after both accepted, confirming a stale version, trading from 10 m away, a full pack.
- Two world servers: a listing made on world 1 is bought on world 2, the seller (logged in on world 1) gets paid; two buyers on two worlds race for
  one listing: exactly one gets it, the other is refunded.
- Class switch keeps the ingots and bank (`Tools/class_net.sh`). Shipping compile.
