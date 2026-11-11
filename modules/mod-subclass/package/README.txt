CoA Subclass package
====================

An add-on for CoA Custom: it updates the subclass work that CoA Custom already ships. It needs CoA Custom to be
installed first and is made for exactly one CoA Custom version (see "What you need").

What it adds
------------
- Subclasses: choose one subclass for your character from level 20 (quest "Book of Origins").
- Two races of its own: Two-Headed Ogre (race 85) and The Twins (race 96).
- Two-Players (new, not tested properly yet): two players share one body. Switch it off on the server with
  Subclass.TwoPlayers.Enable = 0 in mod_subclass.conf.
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
4. It asks whether to install the Two-Players client (a CoA64.exe with the Two-Players looks). Without it,
   everything works, but a Two-Players body does not show its carried mount or the joined name. The original
   CoA64.exe is in the backup.
5. Start the servers as usual and fully restart the game.

Run Install-Subclass.bat again after reinstalling CoA Custom: that replaces the worldserver and the world tables
this package changes.

LiveMon: start LiveMon\LiveMon.exe in the repack folder while the server runs.

Server settings (mod_subclass.conf)
-----------------------------------
- Subclass.Enable: the whole subclass system.
- Subclass.TwoPlayers.Enable: Two-Players. 0 means no new Two-Players characters and no pairing; existing pairs
  play as single characters.

Feedback: tell Stefan (stefanw1337) what you tried, what happened and when.
