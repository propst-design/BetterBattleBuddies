# Better Battle Buddies


> **Source development:** `main` contains ongoing development and can be ahead of
> the Workshop release or contain unfinished changes. For normal play, use Workshop.
> Contributions are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md) for setup,
> testing and pull requests. No direct repository access is needed.

[![Game: Arma Reforger](https://img.shields.io/badge/Arma-Reforger-556b2f?style=flat-square)](https://reforger.armaplatform.com/)
[![Workshop release: 0.1.10](https://img.shields.io/badge/Workshop-0.1.10-blue?style=flat-square)](CHANGELOG.md)
[![Standalone addon](https://img.shields.io/badge/Addon-Standalone-teal?style=flat-square)](#install-and-play)
[![License: APL-SA](https://img.shields.io/badge/License-APL--SA-orange?style=flat-square)](LICENSE.md)

AI that keeps its head on a swivel—and knows when to hit the dirt.

Better Battle Buddies improves AI scanning, weapon readiness, combat reactions,
medical assistance and vehicle handling in Arma Reforger. Use it independently
in vanilla scenarios or as AC: Actual's required AI addon. BBB does not require
AC: Actual and does not add a commander game mode.

## Requirements

- Arma Reforger. BBB needs only the base game: no other mod.
- Built for Arma Reforger **1.9** (the 1.9 fixes have been on Workshop since 0.1.8).
- Optional: AC: Actual (which requires BBB), AiO Command Menu, ACE. Each works with BBB
  but none is needed.

## Install and play

1. Find **Better Battle Buddies** in Arma Reforger's Workshop, download it and enable it.
2. Start a scenario with AI present, such as Conflict, Game Master or a mission of your own.
   Without a host mod BBB starts by itself about 10 seconds into the mission.
3. For multiplayer, load BBB on the server and on joining clients.

The current Workshop release is **0.1.10**; `main` is heading for 0.2.0.
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
- **Opportunistic combat rearming.** Soldiers running critically low on ammunition or lacking AT scavenge compatible magazines and rocket launchers from nearby casualties, retrying routes if interrupted. A soldier who has lost his primary weapon goes back for his own if it is nearby, otherwise takes the nearest usable rifle or machine gun from a body or the ground; with none to be had he is kept out of bounds and building moves.
- **Specialist teams.** Machine gunners and AT gunners keep the assistant who carries their ammunition close; he hands ammunition across before the gun runs dry and takes up the launcher if the gunner falls (`-bbbSpecialistTeams 0` turns it off).
- **Squad leaders run the fight.** An AI squad leader points men who haven't spotted the enemy at it, puts a fireteam's fire on a second enemy group, picks who goes to a casualty (not the machine gunner mid-fight) and splits the squad into the teams that bound. He says what the squad is doing in the game's own radio voices (new orders, contact, man down, suppress, flank, fall back, form up on him) and a man answers; there is no idle chatter, and a player's own squad gets none of it (`-bbbCallouts 0`, `-bbbCalloutRate`).
- **Squad plans.** A squad pauses on first contact, then assaults when the odds are fair or holds and fights from cover when outnumbered, flanked or pinned; a squad held by a dug-in enemy fixes it and sends a team round the side, and one that loses contact searches where the enemy was last seen.
- **Fighting zombies.** With Project Outbreak or another Creature Melee AI creature mod loaded, soldiers fight the dead differently from men: no surrender to them, backing away from one that closes in, no flanking, bounding or suppressive fire against them, and no treating, dragging or looting with one close. Men in the same fight are still fought the usual way (`-bbbZombies 0` turns it off, `1` forces it on).
- **Cover that faces the enemy.** A soldier only counts as in cover when the cover is between him and the enemy; one caught on an open road in a firefight moves off it, and a gunner on a dry static gun gets off and fights on foot.

The README describes `main`; some of the behaviour above arrives on Workshop with the next
release. [CHANGELOG.md](CHANGELOG.md) shows what is released and what is not yet.

These behaviors remain subject to the game's AI, navigation, available equipment
and current orders. They do not guarantee that every rescue, engagement or vehicle
recovery succeeds.

## Optional features and compatibility

Almost every feature is on by default. Every one has its own switch, so a server owner can
turn off just the part that clashes with a mission or another mod. ACE is optional;
ACE-dependent behaviour needs ACE loaded.

A few features are **off by default** and must be switched on:

| Option | What it adds |
| --- | --- |
| `-bbbHotAI 1` | Hot AI, spotting and fire timing: soldiers spot faster and keep their eyes when shot at, shoot sooner, fire longer bursts |
| `-bbbHotVehicles 1` | Hot AI, gunners and vehicles: turret gunners get time to aim, hold their target, feel hits on their vehicle and use the main gun; a hit armed vehicle slows down |
| `-bbbReturnFire 1` | A soldier shot at by an enemy he can't see fires a short burst back toward the shots |
| `-bbbGiveWay 1` | Friendly AI vehicles on a collision course agree who gives way |
| `-bbbEvadeArmour 1` | An unarmed vehicle that sees an armed enemy vehicle asks the host mod to drive it away (does nothing without a host mod that handles it) |

A Game Master can also switch both Hot AI options for the rest of a mission: they are
checkboxes in the Game Master's game settings.

AC: Actual integrates through BBB's host hooks and manages its own commander,
bases, logistics and vehicle-driving decisions. AiO Command Menu is optional.
A player's or Game Master's explicit order always takes priority over BBB's own reactions.

### Settings tab and presets

Open **Settings > Battle Buddies** to change any BBB option without launch parameters. Each
option has a short description; the panel on the right also shows its default and the launch
parameter that sets it on a server.

- **Presets.** Pick the preset in use at the top. *BBB default* is what the mod ships and
  *Vanilla* turns every BBB feature off; both are read-only. Press **Duplicate** (or **New**) to
  make your own, then change anything: edits save straight away. **Delete** asks for a second
  press. **Reload** picks up preset files you added or edited outside the game.
- **When it applies.** The preset in use applies when a mission starts, never in the middle of
  one, in singleplayer and in games you host. On someone else's server that server's settings
  apply; the tab says so when you open it there.
- **Sharing.** **Export** copies the preset to the clipboard as JSON and shows its file:
  `profile/BBB/Presets/<name>.json` in your Arma Reforger profile folder
  (Documents/My Games/ArmaReforger on Windows). Paste it to a friend, who saves it to the same
  folder and presses Reload.

**Hosting a server with a preset.** Copy the preset file into the server's profile folder under
`BBB/Presets/` and add `-bbbPreset <name>` to the server's launch parameters, using the file
name without `.json` or the preset's name in quotes. The built-in presets need no file:
`-bbbPreset Vanilla`. Priority is launch parameters, then the preset, then BBB's defaults, so a
server that only uses `-bbb...` options keeps working exactly as before. The server log shows
which preset loaded (`[BBB] SETTINGS preset ...`).

### Server options

Player control, explicit player/GM orders and host-mod policy take priority over
autonomous features. Every option in the Settings tab is also a launch parameter (the
tab's right-hand panel names it); the tab lists them all, in twelve groups. The ones
server owners reach for most, each turning its feature off with `0`:

| Option | What it disables |
| --- | --- |
| `-bbbVanilla 0` | BBB starting on its own without a host mod (BBB then only runs under a host mod such as AC: Actual) |
| `-bbbTactic 0` | Squad plans: assault, hold, fix and flank, search |
| `-bbbSquadFire 0` | The squad leader splitting his squad's fire between enemy groups |
| `-bbbCallouts 0` | Squad leaders' voice callouts (`-bbbCalloutRate` 0.25 to 3 makes them rarer or busier) |
| `-bbbOrderLeash 0` | Keeping reactions inside the area and intent of an order |
| `-bbbAimFixes 0` | All of BBB's aim changes (vanilla aim) |
| `-bbbThreat 0` | All of BBB's suppression changes (vanilla suppression) |
| `-bbbExplosionReflex 0` | Hitting the dirt as a blast wave arrives |
| `-bbbRearm 0` | Rearming from fallen friendlies |
| `-bbbRearmWeapon 0` | Soldiers who lost their weapon picking one up |
| `-bbbSpecialistTeams 0` | Gunner and ammo-bearer teams |
| `-bbbDrag 0` | Dragging casualties to cover |
| `-bbbGunnerReplace 0` | Squadmates taking over an empty gunner seat |
| `-bbbTacticalDismount 0` | Passengers dismounting from a vehicle slowing under fire |
| `-bbbUnstick 0` | Stuck AI drivers reversing away; soldiers stepping off truck beds |
| `-bbbHandbrake 0` | Handbrake on when an AI driver leaves (an empty vehicle rolls on a slope from 1.9) |
| `-bbbGunWinchester 0` | A gunner leaving a static gun that is out of ammo |
| `-bbbRoadExit 0` | Moving off an open road in a firefight |
| `-bbbBios 0` | Soldier bios |
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
| `-bbbWithdrawOrder 0` | Covered fall-back when the host mod orders a squad back under fire (only on a host mod's request) |
| `-bbbMoraleSurrender 0` | Cut-off, pinned soldiers surrendering |
| `-bbbMoraleSurrenderFates 0` | Surrenders that go wrong: executions and fake surrenders |
| `-bbbZombies 0` | Zombie rules (on by default when Project Outbreak or Creature Melee AI is loaded; `1` forces them on) |
| `-bbbMoraleSurrenderAce 0` | Prisoners using ACE Captives' surrender (hands up, cuffable) when ACE is loaded |
| `-bbbBuildings 0` | Contact-driven indoor cover movement |
| `-bbbFlares 0` | BBB's autonomous night-illumination requests |
| `-bbbDoorUnstick 0` | Opening or flipping doors that block movement |
| `-bbbContactRelay 0` | Squads sharing spotted enemies by radio |
| `-bbbCrewTurnout 0` | Quiet-period crew turnout and danger-driven buttoning up |
| `-bbbDriverAlign 0` | Small idle-driver adjustments for a gunner's visible target |
| `-bbbDropTubes 0` | AI dropping a spent single-shot launcher |
| `-bbbArmorReaction 0` | Fitted smoke and guarded wheeled-vehicle reversal against launcher fire |
| `-bbbCommanderCrew 0` | Treating turret-linked commander positions as crew |
| `-bbbExposedFire 0` | Continued spotting and stronger return fire when suppressed without cover |
| `-bbbFireDiscipline 0` | Fire discipline: soldiers shot at by an unseen enemy get into cover before firing back, only two or three per squad fire blind and only within rifle or machine-gun range, spare magazines are kept, suppressing a spotted group stops at long range, and past 100 m bursts get shorter and aiming pauses longer |
| `-bbbTrueCover 0` | Cover only counts when it is between the soldier and the enemy he knows about |
| `-bbbShoreRoutes 0` | Foot routes round water keeping off low shore ground |

Indoor movement uses native cover and navigation, abandons failed routes and does
not guarantee every room is cleared. Grenades, smoke and flares use carried kit.
Vehicle smoke needs supported fitted weapons. Driver alignment requires clear,
level ground and is limited to 2.5 m, 20 degrees and eight seconds; armour reactions
support a guarded short reverse for slow wheeled vehicles, not tracked driving.

The **Vanilla AI** group sets the skill (`-bbbAISkill`) and spotting speed
(`-bbbAIPerception`) every AI soldier spawns with; a host mod, Game Master or mission that
sets a soldier's own skill still wins.

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

Directed by **Jacob** ([propst-design](https://github.com/propst-design)), co-developed with
**SlyZilla** ([SlyZilla](https://github.com/SlyZilla)).
Extracted from AC: Actual on 15 October 2026. Licensed under APL-SA; see
[LICENSE.md](LICENSE.md) for the retained original attribution and project credits.
