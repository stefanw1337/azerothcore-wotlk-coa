CoA Subclass test package
=========================

What it adds
------------
- Subclasses: choose one subclass for your character from level 20 (quest "Book of Origins").
- Two new races: Two-Headed Ogre (race 85, female) and Thin Twins (race 96, male).
- LiveMon: a window that shows how every spec, with and without a subclass, does on your server
  (damage, healing, crowd control, deaths). It stays on your PC; nothing is sent anywhere.

What you need
-------------
- A CoA Manager repack with CoA Custom 1.9.2 races (server and game client).
  The Jealous-Sound repack with CoA-Bots is not supported by this test build yet.

Install
-------
1. Stop the servers and close the game.
2. Extract this zip into the repack folder, so that CoA-Subclass sits next to the Scripts folder.
3. Run CoA-Subclass\Install-Subclass.bat. It asks for the game folder (the one with Ascension.exe).
   It backs up your databases and every file it replaces into CoA-Subclass\backups first.
4. Start the servers as usual and fully restart the game.

Run Install-Subclass.bat again after every CoA Manager update or CoA Custom reinstall:
both replace the worldserver and world tables this package changes.

LiveMon: start LiveMon\LiveMon.exe in the repack folder while the server runs.

Known issues
------------
- Thin Twins use CoA Custom 1.9.2's new Thin Human model as it is; their cape and helmet fit are not redone yet.
- When Auto Shot starts, the gun of a Hunter subclass jumps to the other hand for a moment.

Feedback: tell Stefan (stefanw1337) what you tried, what happened and when.
