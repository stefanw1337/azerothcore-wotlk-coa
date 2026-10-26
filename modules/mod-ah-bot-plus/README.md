# Auction House Bot for CoA

Fills auction houses and optionally buys player listings. Based on
[NathanHandley/mod-ah-bot-plus](https://github.com/NathanHandley/mod-ah-bot-plus).

Configure it in the Manager module settings or `mod_ahbot.conf`:

- Turn on `AuctionHouseBot.Enable`. With `AuctionHouseBot.GUIDs = 0` the server creates its own auction
  character (account `AHBOT`) on the next start; set `GUIDs` to dedicated existing characters to use your own.
  Keep these characters offline; bot auction mail is discarded by default.
- Set Alliance, Horde and Neutral listing limits. Defaults are 2,000 per house.
- Enable the buyer separately if desired; it is off by default.

`AuctionHouseBot.CoA.ObtainableItemsOnly` defaults to true. The seller uses item IDs from
CoA world loot tables, vendors and profession create-item spells. Custom IDs are supported without
an arbitrary upper ID cutoff. Binding, quality, name, price and optional level/ID restrictions still apply.
This excludes templates with no source; it does not prove every source is currently reachable by players.
Turn the filter off only if you deliberately want the complete template pool.

The master switch is off in new installations. Older configurations without it retain their previous
seller/buyer behavior. `.ahbot reload` reloads configuration and candidates; disabling the module stops
new activity but keeps existing auctions until expiration. `.ahbot empty` removes bot-owned auctions.
A shared cross-faction auction house uses the Neutral limits.
