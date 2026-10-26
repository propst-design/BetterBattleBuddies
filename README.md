# Better Battle Buddies


> **Source development:** `main` contains ongoing development and can be ahead of
> the Workshop release or contain unfinished changes. For normal play, use Workshop.
> Contributions are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md) for setup,
> testing and pull requests. No direct repository access is needed.

[![Game: Arma Reforger](https://img.shields.io/badge/Arma-Reforger-556b2f?style=flat-square)](https://reforger.armaplatform.com/)
[![Workshop release: 0.1.6](https://img.shields.io/badge/Workshop-0.1.6-blue?style=flat-square)](CHANGELOG.md)
[![Standalone addon](https://img.shields.io/badge/Addon-Standalone-teal?style=flat-square)](#install-and-play)
[![License: APL-SA](https://img.shields.io/badge/License-APL--SA-orange?style=flat-square)](LICENSE.md)

AI that keeps its head on a swivel—and knows when to hit the dirt.

Better Battle Buddies improves AI scanning, weapon readiness, combat reactions,
medical assistance and vehicle handling in Arma Reforger. Use it independently
in vanilla scenarios or as AC: Actual's required AI addon. BBB does not require
AC: Actual and does not add a commander game mode.

## Install and play

1. Find **Better Battle Buddies** in Arma Reforger's Workshop, download it and enable it.
2. Start a vanilla scenario such as Conflict or Game Master with AI present.
3. For multiplayer, load BBB on the server and joining clients.

The current Workshop release is **0.1.6**.
See [CHANGELOG.md](CHANGELOG.md) for full version history.

## What your buddies do

- **More alert soldiers.** Horizon scanning, weapon-readiness recovery and engagement fixes help AI notice trouble and bring their weapons back into the fight.
- **Smoke before aid.** Medics and teammates deploy smoke toward a threat before advancing to an exposed casualty, waiting for the cloud to bloom.
- **Tactical dismount under fire.** AI passengers in vehicles slowing under fire dismount to fight on foot while turret crews remain aboard.
- **Less getting stuck.** Standalone AI drivers reverse and steer away from obstacles. Door/gate helpers and spent-launcher handling address everyday snags.
- **A little personality.** Soldiers carry personal bios alongside combat improvements.

## Combat reactions and survival

- **Weapon mode reliability.** Fire-selector fixes accept already-selected fallback modes and prevent mode cycling.
- **Fire that keeps heads down.** Elevated, flanking and rear fire affect suppression; ambushes shock calm squads, and suppression takes longer to wear off.
- **Explosions with a beat of distance.** Foot soldiers hit the dirt as the blast wave reaches them using a 343 m/s acoustic delay: prone close in, crouched farther out.
- **Keep the vehicle fighting.** Available AI squadmates autonomously replace empty or incapacitated turret gunners on operational armed vehicles.
- **Get low against heavy armor.** Soldiers caught without anti-armor weapons adopt low stances (crouch out to 450 m, prone within 120 m) to minimize profile when facing armored vehicles, while rocket specialists hold position to engage.
- **Opportunistic combat rearming.** Soldiers running critically low on ammunition or lacking AT scavenge compatible magazines and rocket launchers from nearby casualties, retrying routes if interrupted.

These behaviors remain subject to the game's AI, navigation, available equipment
and current orders. They do not guarantee that every rescue, engagement or vehicle
recovery succeeds.

## Optional features and compatibility

Combat, medical and vehicle features are enabled by default. Server owners can
disable them individually with the options below. The optional Think panel stays
off unless enabled. ACE is optional; ACE-dependent behaviour needs ACE loaded.

AC: Actual integrates through BBB's host hooks and manages its own commander,
bases, logistics and vehicle-driving decisions. AiO Command Menu is optional.

### Server options

Player control, explicit player/GM orders and host-mod policy take priority over
autonomous features. Each option below accepts `0` to disable its feature.

Server owners can disable features individually:

| Option | What it disables |
| --- | --- |
| `-bbbFireManeuver 0` | Alternating covering fire and movement |
| `-bbbFireCoverOrders 0` | Ordered covering fire alone; keeps the rest of fire and manoeuvre |
| `-bbbCombatManeuver 0` | Combat repositioning |
| `-bbbMedic 0` | Safer healing and casualty-aid decisions |
| `-bbbMedicSmoke 0` | Smoke before approaching exposed casualties |
| `-bbbSmoke 0` | Tactical smoke for drags, bounds and withdrawals |
| `-bbbGrenades 0` | Autonomous fragmentation-grenade use |
| `-bbbStaticWeapons 0` | Autonomous static-weapon crewing |
| `-bbbHoldGuns 0` | Gunners staying on static guns a host mod marks as held |
| `-bbbMorale 0` | Morale-driven withdrawal and surrender |
| `-bbbBuildings 0` | Contact-driven indoor cover movement |
| `-bbbFlares 0` | BBB's autonomous night-illumination requests |
| `-bbbDoorUnstick 0` | Opening or flipping doors that block movement |
| `-bbbContactRelay 0` | Squads sharing spotted enemies by radio |
| `-bbbCrewTurnout 0` | Quiet-period crew turnout and danger-driven buttoning up |
| `-bbbDriverAlign 0` | Small idle-driver adjustments for a gunner's visible target |
| `-bbbArmorReaction 0` | Fitted smoke and guarded wheeled-vehicle reversal against launcher fire |
| `-bbbCommanderCrew 0` | Treating turret-linked commander positions as crew |
| `-bbbExposedFire 0` | Continued spotting and stronger return fire when suppressed without cover |

Indoor movement uses native cover and navigation, abandons failed routes and does
not guarantee every room is cleared. Grenades, smoke and flares use carried kit.
Vehicle smoke needs supported fitted weapons. Driver alignment requires clear,
level ground and is limited to 2.5 m, 20 degrees and eight seconds; armour reactions
support a guarded short reverse for slow wheeled vehicles, not tracked driving.

Hosts and mission makers can enable `-bbbThink 1` and look at a soldier to see BBB's
decisions and reasons for holding back. `-bbbThink 2` also writes notes to the
server log. Leave the option unset to keep it off.

## Reporting problems

Report in **[Discord](https://discord.gg/WK8xGSztB)** under **#issues** (choose **BBB**),
or post ideas in **#suggestions**. No GitHub account needed. Search existing Discord posts first.

Prefer GitHub? **[Report a bug](https://github.com/propst-design/BetterBattleBuddies/issues/new?template=01-bug-report.yml)**
or **[suggest a feature](https://github.com/propst-design/BetterBattleBuddies/issues/new?template=02-feature-request.yml)**.
Search [existing issues](https://github.com/propst-design/BetterBattleBuddies/issues?q=is%3Aissue)
first; add details to an existing report when it describes the same problem.

For either route, include BBB/game versions, platform, scenario, other mods,
hosting/joining context, steps to reproduce, expected behavior and actual behavior.
Report one problem per post or issue. Testing with only BBB and the base game helps isolate
conflicts, but is optional.

Attach `console.log` and `script.log` from the affected session when available.
Normal PC game logs are under `Documents\My Games\ArmaReforger\logs`; custom
profiles and dedicated servers may use another location. For multiplayer, include
server and affected client logs when available. Screenshots or video also help,
especially on consoles. Remove passwords, tokens, IP addresses and personal
information before uploading logs. Missing logs do not prevent reporting a bug.

Extracted from AC: Actual on 15 October 2026. Licensed under APL-SA; see
[LICENSE.md](LICENSE.md) for the retained original attribution and project credits.
