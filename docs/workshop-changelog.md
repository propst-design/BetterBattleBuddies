0.2.0

Surrender that happens and matters, soldiers who fight zombies like zombies, squad leaders who talk, and soldiers who pick a weapon back up. Works with AC: Actual 0.6.0.

NEW

- Surrender: a soldier cut off, pinned down, out of spare magazines or badly wounded, with an enemy close, may give up. He drops his weapons, kneels with a hand raised, then sits; AI on both sides leave him alone. Now and then a prisoner is shot, and now and then one was faking and fights again with a hidden grenade or pistol, so walk up carefully. Options: -bbbMoraleSurrender 0, -bbbMoraleSurrenderFates 0.
- Works with ACE Captives: when it is loaded, prisoners surrender the ACE way and can be cuffed, led away and put in vehicles (-bbbMoraleSurrenderAce 0 keeps the plain kneel and sit).
- Zombies: with Project Outbreak (or another Creature Melee AI mod) loaded, soldiers fight zombies like zombies. Nobody surrenders to them, a soldier backs away from one closing in, squads hold together and shoot instead of bounding or flanking, and medics clear a zombie before they treat or drag a casualty. Men are still fought the usual way in the same fight. Option: -bbbZombies auto, 1 or 0.
- Squad leader callouts: AI squad leaders voice orders, contacts, men down, suppressing fire, flanks and fall-backs in the game's own radio voices, and a man answers. No new recordings, never in a player's voice. Options: -bbbCallouts 0, -bbbCalloutRate.
- Soldiers who lose their rifle or machine gun go back for it, or take one from a fallen soldier along with its magazines. Option: -bbbRearmWeapon 0.
- Experimental, off by default: BBB can drive AI wheeled vehicles itself on long moves along the game's route (-bbbDriver 1, or the settings tab). Tested on 12 route trips only; convoys, towns, combat and multiplayer are untested.

FIXED

- Medics dragging a casualty with ACE Carrying no longer drop him a second into the drag.
- With AC: Actual, a player's own squad is left alone again after he takes over its leader: his men follow him instead of looting, crewing guns or surrendering on their own.
- Soldiers in an unarmed truck no longer all pile out whenever an enemy is known to be near; they get out when the truck is hit or an armed enemy is seen, and climb back in afterwards.
- A faked surrender's grenade goes only at an enemy the soldier can see, medics no longer surrender mid-drag, and long fights keep room for new prisoners.
- New option -bbbDropTubes 0 keeps spent single-shot launchers; AI in a player's squad keep theirs anyway.
- Stopping Battle Buddies mid-match hands vehicles and drivers back cleanly, and the server does less work checking for men stuck on truck beds.

For mod makers: new hooks AllowSurrender, AllowCallouts, ZombieRules, IsZombie and AllowScriptedDriving, plus BBB_Morale.IsPrisoner and BBB_Zombies.
