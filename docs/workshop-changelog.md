0.1.10

Squad leaders who run their squad's fight and vehicle commanders who run the gun. Works with AC: Actual 0.5.12.

NEW

- Squad leaders run their squad's fire. A man who hasn't spotted the enemy his squad is fighting is told where it is and looks there. When a second enemy group shows up on another side, the leader puts a fireteam's fire on it while the machine gunners stay on the main threat, and calls the group out. Options: -bbbSquadCue 0 and -bbbSquadFire 0 (or the Battle Buddies settings tab, Squad tactics).
- The squad leader picks who helps a casualty. In a fight the machine gunner, the leader, the AT gunner (while enemy armour is about) and a short-handed flank team stay on their jobs and the nearest of the rest goes; if nobody else can, the nearest man still goes. Option: -bbbSquadJobs 0.
- The squad leader draws the teams that bound. Fire and manoeuvre and flanking now split the squad into the left and right halves of the line to the enemy, keep a gunner with his assistant, let the machine gunner's half cover first, and leave the fireteam watching a flank out of the bounds. Option: -bbbSquadTeams 0.
- Vehicle crews fight as a crew: the commander (or the driver, if there is no commander) puts together what everyone aboard and his squad nearby can see and gives the gunner the most dangerous target, laying him onto it if he can't see it yet, and calls it out. In a test with a rifleman plinking at a LAV-25 and a squad off to the side, the gunner put 97% of his rounds on the squad instead of 70%. Option: -bbbCrewLeader 0 (or the Battle Buddies settings tab, Vehicles and crews).

BETTER

- Casualties dragged to cover are taken a few metres behind it instead of being left at its open edge, when the way is clear and the spot really hides them. Needs ACE Carrying, as drags always have. Option: -bbbDragDeep 0 brings back the old stopping point.

FIXED

- Commanders stay in their vehicles when a fight starts. The game counts a LAV-25's (or a mod tank's) commander seat as a passenger seat and unloaded it on contact; the crew of an armed vehicle now keep their seats while riders in the back still get out.
- No more script error (Math.RandomFloat: invalid parameters) from a weapon whose fire mode has no shot length.
