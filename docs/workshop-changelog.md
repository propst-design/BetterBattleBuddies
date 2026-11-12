0.1.11

Surrender that happens and matters, soldiers who fight zombies like zombies, squad leaders who talk, and soldiers who pick a weapon back up. Works with AC: Actual 0.5.13.

NEW

- Surrender: a soldier cut off, pinned down, out of spare magazines or badly wounded, with an enemy close, may give up. He drops his weapons, kneels with a hand raised, then sits; AI on both sides leave him alone. Now and then a prisoner is shot, and now and then one was faking and fights again with a hidden grenade or pistol, so walk up carefully. Options: -bbbMoraleSurrender 0, -bbbMoraleSurrenderFates 0.
- Works with ACE Captives: when it is loaded, prisoners surrender the ACE way and can be cuffed, led away and put in vehicles (-bbbMoraleSurrenderAce 0 keeps the plain kneel and sit).
- Zombies: with Project Outbreak (or another Creature Melee AI mod) loaded, soldiers fight zombies like zombies. Nobody surrenders to them, a soldier backs away from one closing in, squads hold together and shoot instead of bounding or flanking, and medics clear a zombie before they treat or drag a casualty. Men are still fought the usual way in the same fight. Option: -bbbZombies auto, 1 or 0.
- Squad leader callouts: AI squad leaders voice orders, contacts, men down, suppressing fire, flanks and fall-backs in the game's own radio voices, and a man answers. No new recordings, never in a player's voice. Options: -bbbCallouts 0, -bbbCalloutRate.
- Soldiers who lose their rifle or machine gun go back for it, or take one from a fallen soldier along with its magazines. Option: -bbbRearmWeapon 0.

FIXED

- Medics dragging a casualty with ACE Carrying no longer drop him a second into the drag.

For mod makers: new hooks AllowSurrender, AllowCallouts, ZombieRules and IsZombie, plus BBB_Morale.IsPrisoner and BBB_Zombies.
