# Changelog

## 0.1.6 — unreleased

- Gunners stay on held guns. A host mod can now tell BBB which static guns a squad holds where it stands, such as a checkpoint MG or a dug-in position (new hook `GetHeldGuns`; plain vanilla and Game Master groups hold none). On those guns the gunner no longer climbs out because an enemy he heard about or saw a while ago is outside the gun's arc: he only gets out for an enemy he saw within 60 m in the last 10 seconds. If a held gun stands empty for 8 seconds, the squad's nearest soldier on foot within 40 m mans it again. BBB's own static crewing keeps a held gun manned when its sector goes quiet. Vehicles are unchanged (-bbbHoldGuns 0).
- Base-game AI fixes: no more script errors when a group's orders are replaced while it is defending ("Wrong class of provided Waypoint!", "Defend Waypoint not provided to the node!"), or when a group without a leader just then fails a move (a NULL pointer in `SCR_AIProcessFailedMovementResult`). These hit any host or Game Master who changes a defending group's orders. The nodes now fail quietly, which is what the errors ended in anyway, so the AI behaves the same.
- Squads warn each other by radio. When a squad spots enemies, friendly squads within its radio range (about 1.3 km with personal radios, 2 km with a manpack) hear about it a few seconds later and react as if they'd heard the shooting: they turn to it, take cover or send a fireteam to look. A report is only where the enemy was last seen, never a target to shoot at, and it isn't passed on again. Squads without a radio only hear each other within shouting distance. Player-led squads and squads under a player's or Game Master's order are left alone (-bbbContactRelay 0).
- Squads can make bounded reactions to nearby contacts during ordinary AI movement orders, then continue their route. Explicit player/GM orders and transport remain in charge. Use -bbbOrderLeash 0 to retain the previous waypoint restrictions.
- Soldiers out of magazines go and get more, even mid-fight. A rifleman with no spare magazine heads for the nearest friendly body with matching ammo instead of carrying on the firefight or being sent forward in a bound, then rejoins his team. Switching to his pistol no longer makes him forget he needs ammo (-bbbRearm 0).
- Squads go round a dug-in enemy. Facing an enemy who stays put 120 to 250 m away, a squad first holds and fires to fix him, then flanks: one half runs to covered ground well off his line while the other half covers it, swapping as they go, and they attack from the new angle. When contact is lost, a squad searches: half of it checks where the enemy was last seen and the cover around it, one spot at a time, while the other half watches, then calls clear. Covering fire is now aimed at the tree, rock or wall nearest where the enemy was seen, and never near friendly soldiers (-bbbTacticFlank 0, -bbbTacticSearch 0, -bbbCoverFire 0).
- Squads commit to a plan. On first contact a squad pauses to take in the enemy, then its leader picks a plan and sticks with it: assault (fire and manoeuvre toward the enemy, about 1.5 minutes) when the odds are fair, or hold and fight from cover (about 45 seconds) when it is outnumbered, flanked, pinned down or the enemy is far off. A squad that finds more enemies than it thought switches from assault to holding. When the leader falls, the squad holds for 20 seconds while someone takes over. The leader calls the plan out loud: move, defend this position, fall back. Squads under a player's or Game Master's order are left alone (-bbbTactic 0).
- Squads read a fight from two sides correctly. A squad facing enemies in more than one direction now aims its smoke, its static gun and its fire-and-manoeuvre at the group that matters most (the nearest, freshest and biggest it has seen) instead of at an empty spot between them, and a squad that breaks retreats through the open side instead of backing away from one group straight into the other (-bbbSquadPicture 0).
- Squads that break really pull back. A squad whose morale breaks now falls back in two halves: one half runs while the other holds its ground and keeps firing, then they swap, even under fire. Before, the runners often never moved and the covering half advanced on the enemy. A lone survivor runs instead of standing still. Losing the squad leader now counts toward a break for a while after someone else takes over (-bbbMorale 0).
- One job at a time. A soldier dragging or treating a wounded buddy, walking to a static gun, or fetching ammo is no longer counted into a fire-and-manoeuvre bound, sent indoors, or picked to throw a grenade or smoke at the same moment. A rescue still takes him from anything else (-bbbClaims 0).
- Vehicle crews stay in their seats. Drivers no longer climb out under fire to open a gate (BBB opens it for them), and the driver, gunner or commander of an armed or armoured vehicle no longer bails out to treat a wounded buddy: someone on foot goes instead. Double gates now open for AI vehicles too, and armour that is under fire or near enemies opens gates well ahead so it doesn't slow down (-bbbCrewStays 0).

## 0.1.5 — released

More initiative under fire, with your orders still in charge. This update enables BBB's combat, medical and vehicle features by default; each retains its own server off switch. The optional Think panel remains off.

### Fire and movement

- Fireteams alternate covering fire and movement. Covering soldiers can suppress a recently reported enemy position after the enemy ducks, movers tolerate brief pauses in cover fire, and a casualty no longer ends the whole manoeuvre. Use -bbbFireManeuver 0 to disable it, or -bbbFireCoverOrders 0 to disable only the ordered covering fire.
- Squads seek indoor cover near detected enemies and investigate further reachable positions when contacts are inside. Blocked routes are abandoned and player orders take priority. This is contact-driven movement, not a guarantee of clearing every room (-bbbBuildings 0 disables).
- Soldiers use carried grenades against nearby enemies in cover, checking friendly clearance and the throwing route before committing. Squad cooldowns limit repeated throws (-bbbGrenades 0).
- Idle or defending soldiers can walk to a nearby loaded friendly static weapon and crew it during contact. They give it up for orders, exhausted ammunition or a lost target (-bbbStaticWeapons 0).
- Badly battered autonomous squads can withdraw in covered bounds; isolated, out-of-ammo survivors cornered by an enemy can surrender. Withdrawals stop if the squad cannot make real progress (-bbbMorale 0).

### Keep each other alive

- Tactical smoke screens casualty drags, advances and withdrawals toward known threat sectors (-bbbSmoke 0). Fixed medic smoke being skipped before the throw could happen.
- Safer healing is now enabled by default (-bbbMedic 0 disables). Medics fight a nearby visible enemy before approaching a casualty, and casualty draggers let go to return fire against an immediate threat.
- Medics no longer backtrack more than 75 m to a casualty during a fight. They can still go when the squad closes up or the fighting quiets down.
- Suppressed soldiers caught without cover keep spotting and fighting back; soldiers in cover still keep their heads down (-bbbExposedFire 0).
- Riflemen no longer permanently give up on jeeps and Humvees after their shots strike the hull. Only real armour triggers rifle-immunity handling. The get-low reaction now requires recently seen armour, while AT specialists retain their firing stance (-bbbArmourScatter 0).
- Night illumination uses carried flare ammunition to light recently sighted enemy sectors, with squad and area cooldowns to limit repeated launches (-bbbFlares 0).

### Crews, doors and squad cohesion

- Supported armoured crew positions turn out after a quiet period and button up when danger returns (-bbbCrewTurnout 0).
- Idle wheeled-vehicle drivers can make a small, guarded adjustment to bring a gunner's visible target inside a limited turret arc. Player driving, movement orders and unsafe ground take priority (-bbbDriverAlign 0).
- Against launcher fire, supported vehicles use fitted smoke weapons; slow wheeled vehicles can also make a short guarded reverse (-bbbArmorReaction 0).
- Turret-linked commander seats count as crew, keeping their occupants aboard when passengers dismount under fire (-bbbCommanderCrew 0).
- Soldiers stuck at a house door while trying to move can swing it fully open or flip it to the other side. Doors are never closed on anyone (-bbbDoorUnstick 0).
- Squads use an awake member as their reference when their leader is knocked out. Soldiers bounding or withdrawing are no longer diverted to scavenge or crew static weapons.
- Fixed a weapon-mode issue that let AI aim a very fast-firing weapon without ever firing.

### See the decision, share the feedback

- Hosts and mission makers can enable -bbbThink 1 and look at an AI soldier to see BBB's decisions and reasons for holding back. Use -bbbThink 2 to include the notes in the server log; the panel is off by default.
- Bugs and ideas are welcome on Discord: https://discord.gg/WK8xGSztB. Use #issues for BBB bugs and #suggestions for ideas. GitHub Issues remain available.

## 0.1.4 — released

- Expanded the Workshop overview with standalone scenario use, squad-command compatibility, server options and public GitHub source/issue links.
- Prevent repeated AI fire-selector requests when the weapon already uses the best available fallback mode.
- Added tactical suppression geometry and recovery tuning (`BBB_Threat`, `-bbbThreatGeometry 0`, `-bbbThreatDecay 0`): incoming fire from elevated high ground or exposed flanks/rear pins AI faster, ambushes against calm or marching squads deal initial suppression shock, nearby explosions carry heavier suppression weight, and suppression decays more gradually so base-of-fire tactics keep heads down longer.
- Added 343 m/s acoustic speed-of-sound explosion reflex (`BBB_Threat`, `-bbbThreatExplosion 0`): foot soldiers near a detonation hit the dirt (prone within 45 m, crouch out to 75 m) as the blast wave physically reaches them rather than reacting on the same frame or remaining standing.
- Added tactical turret gunner seat replacement (`BBB_VehicleHelpers`, `-bbbGunnerReplace 0`): when an armed vehicle's turret gunner is killed, knocked unconscious, or empty with available ammo, AI crew members autonomously replace them. Conscious cargo passengers aboard (preferring non-leaders) are promoted first; if empty and stationary, nearby dismounted squadmates on foot within 8 m mount the turret. Dead or incapacitated gunners are cleanly evicted before boarding.
- Turret logs now describe accepted boarding requests accurately, and development diagnostics track actual occupancy and later dismounts.
- Added anti-armor low-profile posture reaction and AT isolation (`BBB_Armour`, `-bbbArmourScatter 0` / `-bbbAntiArmor 0`): non-AT infantry get low (crouch out to 450 m, prone within 120 m) to minimize profile when confronted by hostile armored vehicles, while dedicated AT specialists hold stance without posture penalties to line up shots.
- Added opportunistic combat rearming and corpse scavenging (`BBB_Rearm`, `-bbbRearm 0`, `-bbbRearmRadius <m>`): soldiers running critically low on ammunition (< 2 magazines) or facing armored threats without AT scavenge nearby friendly/enemy casualties (default 25 m radius) for compatible magazines, launchers, and rockets, checking reach, floor height and obstructions before transfers. Scavenging yields to player/GM orders and control changes, limits failed trips with temporary backoff, restores items after failed inventory insertion, and matches live rocket ammunition to the selected launcher instead of equipment names.

## 0.1.2 — 2026-10-15

- Medics and teammates now deploy smoke cover toward enemy threats by default before advancing to treat an exposed casualty, waiting briefly for the smoke cloud to bloom.
- Added standalone tactical cargo dismount under fire: AI passengers in ground vehicles that slow down or lose their driver under fire automatically dismount to fight on foot while turret crews remain aboard.
- Added standalone speed-aware stalled vehicle unsticking: AI drivers stuck against obstacles reverse and steer clear automatically in Conflict, Game Master, and custom scenarios without requiring Arma Commander: Actual.

## 0.1.1 — 2026-10-15

- Updated the Workshop thumbnail.

## 0.1.0 — 2026-10-15

- First standalone Workshop release, extracted from Arma Commander: Actual.
- Includes existing horizon scanning, weapon readiness, engagement/suppression improvements, vehicle helpers, spent-launcher handling and character bios.
- Preserves the existing defaults for optional combat and medical features. ACE remains optional; ACE-dependent features require ACE.
- Provides the shared AI dependency used by the split ACA release. BBB itself does not require ACA.

Standalone startup and integrated ACA regression checks passed before extraction rollout. Installed gameplay and multiplayer acceptance are tracked separately.
