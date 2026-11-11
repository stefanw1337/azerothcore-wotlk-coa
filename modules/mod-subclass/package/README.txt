CoA Subclass package
====================

An add-on for CoA Custom: it updates the subclass work that CoA Custom already ships. It needs CoA Custom to be
installed first and is made for exactly one CoA Custom version (see "What you need").

What it adds
------------
- Subclasses: choose one subclass for your character from level 20 (quest "Book of Origins").
- Two races of its own: Two-Headed Ogre (race 85) and The Twins (race 96).
- Two-Players (new, not tested properly yet): two players share one body. Switch it off on the server with
  Subclass.TwoPlayers.Enable = 0 in mod_subclass.conf. Carried mounts and the joined name over a shared body need
  the CoA64 client of a CoA Custom release that includes them.
- A bot can be the other head (playerbots): it keeps grinding, questing, looting and resting unless told to stay,
  and its bot menu is on your player frame. The bot quest route is new and untested.
- CoAC Bot Plan addon: /botplan shows what your group's bots plan to do.
- LiveMon: a window that shows how every spec, with and without a subclass, does on your server
  (damage, healing, crowd control, deaths). It stays on your PC; nothing is sent anywhere.

What you need
-------------
- A repack with CoA Custom 1.9.9 installed with its own installer (CoA-Custom\Install-Custom.bat).
  The installer checks this and stops on any other version. A newer CoA Custom needs the subclass package made
  for it.

Install
-------
1. Stop the servers and close the game.
2. Extract this zip into the repack folder, so that CoA-Subclass sits next to CoA-Custom and Scripts.
3. Run CoA-Subclass\Install-Subclass.bat. It finds the game folder CoA Custom was installed to and asks to confirm.
   It backs up your databases and every file it replaces into CoA-Subclass\backups first.
4. It offers to build the Two-Players male Two-Headed Ogre (see below). Then start the servers as usual and fully
   restart the game.

Run Install-Subclass.bat again after reinstalling CoA Custom: that replaces the worldserver and the world tables
this package changes.

The male Two-Headed Ogre
-----------------------
His body is the Heroes of the Storm Cho'Gall, so it cannot ship in this package. The installer builds him on your
PC from your own Heroes of the Storm install and World of Warcraft's Cho'Gall, which it downloads from Blizzard
(about 2 MB), into Data\patch-ZZZZZY-Ogre.MPQ. It needs Heroes of the Storm installed and takes a few minutes.
Without it everything else works, but a male Two-Players Ogre has no body. Build him later with
CoA-Subclass\files\ogre\Build-Ogre.bat.

LiveMon: start LiveMon\LiveMon.exe in the repack folder while the server runs.

Server settings (mod_subclass.conf)
-----------------------------------
- Subclass.Enable: the whole subclass system.
- Subclass.TwoPlayers.Enable: Two-Players. 0 means no new Two-Players characters and no pairing; existing pairs
  play as single characters.

Commands
--------
Two-Players (any player, in chat; the addon's /pair sends the same):
  .pair invite [Name]     ask a player (or your target) to share your body; /pair with a player targeted does it too
  .pair accept | decline  answer an invitation
  .pair status            who you are paired with and who sits where
  .pair stop | go         as Ranged Head on a bot-steered body: the body stops and waits / roams, fights and quests
  .pair echo              list the echo gear: what the body wore when your former Melee Head left

Addon (Ascension_Subclass):
  /subclassbar            help; reset (position), clear (refill with your subclass spells), castbar (unlock the
                          subclass cast bar to Alt-drag it, again to lock), hide | show (Subclass Bar 1; borrowed
                          spells then go on your own bars)
  /subclass state         what the addon sees (subclass, relic, character window); also: aim, casts, art, icons,
                          stancebar reset, sheet reset
  Key Bindings > Subclass Bar 1   keys for the subclass bar

Bot plan addon (CoAC_BotPlan, for SquidBots Lite bots):
  /botplan  or  /bp       show or hide what your group's bots plan to do, with "Do now"
  The bot body's own bot menu is the small button on your player frame (SquidBots Lite's menu).

Carried mounts (the Two-Players Ogre carries his mount; needs a CoA Custom client with carried mounts). Type them
while carrying; each change is saved per mount display in CarriedMounts.txt next to the game:
  /console coaCarryScale <n>   size            /console coaCarryX | coaCarryY | coaCarryZ <n>   position
  /console coaCarryYaw | coaCarryPitch | coaCarryRoll <degrees>                                   turn
  /console coaCarryAs <display id>    show another mount model in its place
  /console coaCarryPose <animation id>   /console coaCarryFrame <n>   hold one pose (still mounts)
  Example, D.R.I.V.E. cars on their side (the default): coaCarryScale 0.8, coaCarryZ 1.3, coaCarryRoll 90.
  /carry   the same in a window (also beside the mount journal): size, turn and position buttons, Profiles (fits
           made for many mounts), Reset (back to the built-in fit) and a share code to copy or paste

Game masters (.subclass):
  .subclass info | specs <class> | set <subclass> <spec> [second spec] | clear | spec <spec> | learn <entry> <rank>
  .subclass quest | insight <count> | bots [all] | reload (administrator)
  .subclass twin [wear <slot> <item> | strip [slot] | copy | info]   the Twins brother's look
  .subclass brain | audit | gun   diagnostics of the other head

Feedback: tell Stefan (stefanw1337) what you tried, what happened and when.
