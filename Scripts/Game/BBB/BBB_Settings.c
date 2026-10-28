// Settings and presets (BBB #8, 27 Oct 2026). Every BBB option is listed once here (key, group,
// default, range, plain-language help) and every feature reads it through BBB_Settings.Get
// instead of System.GetCLIParam, so the Settings tab, preset files and launch parameters are one
// system. Priority: launch parameter > the active preset > the default written here.
// Presets are JSON files in $profile:BBB/Presets. The server applies one when a mission starts:
// `-bbbPreset <name>` picks it on any server; in singleplayer or a player-hosted game the
// preset chosen in the Settings tab ($profile:BBB/ActivePreset.txt) is used when no -bbbPreset
// is given. A client on someone else's server reads only its own launch parameters: the
// server's preset decides. Nothing changes mid-mission: features read their options once per
// mission (Due) and the settings reload when the world is torn down.
// A default here must equal the field's initial value in the feature's own file: a feature
// still keeps that value when nothing is set, so first-mission behaviour is unchanged.

enum BBB_ESettingKind
{
 BOOL,
 NUMBER,
 CHOICE
}

class BBB_Setting
{
 string m_sKey;
 string m_sGroup;
 string m_sLabel;
 string m_sHelp;
 string m_sDefault;
 // The value the built-in "Vanilla" preset uses (empty: the default).
 string m_sVanilla;
 BBB_ESettingKind m_eKind;
 float m_fMin;
 float m_fMax = 1;
 float m_fStep = 1;
 ref array<string> m_aValues = {};
 ref array<string> m_aLabels = {};

 BBB_Setting Range(float min, float max, float step)
 {
  m_fMin = min;
  m_fMax = max;
  m_fStep = step;
  return this;
 }

 // "0|1|2", "Off|On|On, with log notes"
 BBB_Setting Choices(string values, string labels)
 {
  m_eKind = BBB_ESettingKind.CHOICE;
  values.Split("|", m_aValues, false);
  labels.Split("|", m_aLabels, false);
  return this;
 }

 BBB_Setting Vanilla(string value)
 {
  m_sVanilla = value;
  return this;
 }

 // The value as the Settings tab shows it.
 string Shown(string value)
 {
  if (m_eKind == BBB_ESettingKind.BOOL)
  {
   if (value == "0") return "Off";
   return "On";
  }
  if (m_eKind == BBB_ESettingKind.CHOICE)
  {
   int i = m_aValues.Find(value);
   if (i >= 0 && i < m_aLabels.Count()) return m_aLabels[i];
  }
  return value;
 }

 // A value read from a file or typed by a host, brought inside what this setting accepts.
 string Clean(string value)
 {
  value = value.Trim();
  if (m_eKind == BBB_ESettingKind.BOOL)
  {
   value.ToLower();
   if (value == "1" || value == "true" || value == "on") return "1";
   if (value == "0" || value == "false" || value == "off") return "0";
   return m_sDefault;
  }
  if (m_eKind == BBB_ESettingKind.CHOICE)
  {
   if (m_aValues.Contains(value)) return value;
   return m_sDefault;
  }
  if (value.IsEmpty()) return m_sDefault;
  return BBB_Settings.Number(Math.Clamp(value.ToFloat(), m_fMin, m_fMax));
 }
}

class BBB_Settings
{
 static const string DIR = "$profile:BBB/Presets";
 static const string ACTIVE_FILE = "$profile:BBB/ActivePreset.txt";
 static const string DEFAULT_PRESET = "BBB default";
 static const string VANILLA_PRESET = "Vanilla";

 protected static ref array<ref BBB_Setting> s_aSettings;
 protected static ref map<string, BBB_Setting> s_Index;
 // The values this mission runs with (from the active preset; launch parameters are read live).
 protected static ref map<string, string> s_Values;
 // Keys a preset ever set in this game session: a later mission without them gets the default
 // back, because the feature still holds the earlier mission's value.
 protected static ref set<string> s_Touched;
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Values = new map<string, string>();
  s_Touched = new set<string>();
 }
 protected static int s_iGeneration;
 protected static bool s_bStale = true;
 protected static string s_sPreset;
 // Tests only: load this preset (empty: none) instead of -bbbPreset or the Settings tab's pick.
 static bool s_bTestPick;
 static string s_sTestPreset;

 // ---- reading (features) ----

 // System.GetCLIParam with the preset behind it: true when the host or the active preset sets
 // the option. Features keep their own default when it returns false.
 static bool Get(string key, out string value)
 {
  Refresh();
  if (System.GetCLIParam(key, value))
  {
   // Cleaned like a preset value: "-bbbSmoke true" or a bare "-bbbMedic" read as off where
   // features test == "1", and numbers went past their range.
   BBB_Setting given = Find(key);
   if (given) value = given.Clean(value);
   return true;
  }
  if (s_Values.Find(key, value)) return true;
  if (!s_Touched.Contains(key)) return false;
  BBB_Setting setting = Find(key);
  if (!setting) return false;
  value = setting.m_sDefault;
  return true;
 }

 // A feature's once-per-mission read guard: `if (!BBB_Settings.Due(s_iRead)) return;`.
 static bool Due(inout int seen)
 {
  Refresh();
  if (seen == s_iGeneration) return false;
  seen = s_iGeneration;
  return true;
 }

 // The world is going away (ArmaReforgerScripted.OnBeforeWorldCleanup): the next mission
 // reloads the preset and every feature reads its options again.
 static void Invalidate()
 {
  s_bStale = true;
 }

 static string ActivePreset()
 {
  Refresh();
  return s_sPreset;
 }

 protected static void Refresh()
 {
  if (s_bStale) Load();
 }

 protected static void Load()
 {
  s_bStale = false;
  s_iGeneration++;
  s_Values.Clear();
  s_sPreset = "";
  // Clients on someone else's server: the server's preset decides, not this player's.
  if (!Replication.IsServer()) return;
  string name;
  if (s_bTestPick) name = s_sTestPreset;
  else if (!System.GetCLIParam("bbbPreset", name) && RplSession.Mode() != RplMode.Dedicated) name = ActiveName();
  if (name.IsEmpty()) return;
  BBB_Preset preset = BBB_Preset.Find(name);
  if (!preset)
  {
   PrintFormat("[BBB] SETTINGS preset '%1' not found in %2: using defaults", name, DIR, level: LogLevel.WARNING);
   return;
  }
  s_sPreset = preset.m_sName;
  foreach (string key, string value : preset.m_Values)
  {
   BBB_Setting setting = Find(key);
   if (!setting) continue;
   s_Values.Set(key, setting.Clean(value));
   s_Touched.Insert(key);
  }
  PrintFormat("[BBB] SETTINGS preset '%1': %2 values (launch parameters override them)", s_sPreset, s_Values.Count());
 }

 // ---- the registry ----

 static array<ref BBB_Setting> All()
 {
  if (!s_aSettings) Define();
  return s_aSettings;
 }

 static BBB_Setting Find(string key)
 {
  if (!s_aSettings) Define();
  return s_Index.Get(key);
 }

 // The groups in the order the Settings tab shows them.
 static array<string> Groups()
 {
  array<string> groups = {};
  foreach (BBB_Setting setting : All())
  {
   if (!groups.Contains(setting.m_sGroup)) groups.Insert(setting.m_sGroup);
  }
  return groups;
 }

 protected static BBB_Setting Def(string group, string key, BBB_ESettingKind kind, string def, string label, string help)
 {
  BBB_Setting setting = new BBB_Setting();
  setting.m_sGroup = group;
  setting.m_sKey = key;
  setting.m_eKind = kind;
  setting.m_sDefault = def;
  setting.m_sLabel = label;
  setting.m_sHelp = help;
  // Every feature switch is off in the Vanilla preset.
  if (kind == BBB_ESettingKind.BOOL && def == "1") setting.m_sVanilla = "0";
  s_aSettings.Insert(setting);
  s_Index.Set(key, setting);
  return setting;
 }

 protected static BBB_Setting On(string group, string key, string label, string help)
 {
  return Def(group, key, BBB_ESettingKind.BOOL, "1", label, help);
 }

 protected static BBB_Setting Off(string group, string key, string label, string help)
 {
  return Def(group, key, BBB_ESettingKind.BOOL, "0", label, help);
 }

 protected static BBB_Setting Num(string group, string key, float def, float min, float max, float step, string label, string help)
 {
  return Def(group, key, BBB_ESettingKind.NUMBER, Number(def), label, help).Range(min, max, step);
 }

 protected static void Define()
 {
  s_aSettings = {};
  s_Index = new map<string, BBB_Setting>();
  string g = "General";
  On(g, "bbbVanilla", "Run without a host mod", "Starts BBB on its own 10 s into a mission when no host mod (such as AC: Actual) starts it. Off: BBB only runs under a host mod.");
  On(g, "bbbClaims", "One job per soldier", "A soldier dragging a casualty, crewing a static gun or fetching ammo isn't also picked for a bound, sent indoors or picked to throw.");
  On(g, "bbbOrderLeash", "Reactions stay within orders", "AI reactions stay inside the area and intent of a player's, Game Master's or host's order.");
  On(g, "bbbBios", "Soldier bios", "Soldiers carry personal bios.");
  Num(g, "bbbIdentityEggs", 1, 0, 3, 0.25, "Names, birthdays and blood types in bios", "How often bios give a soldier a name, birthday or blood type. 0 keeps the bios without them; above 1 makes them more common.");

  g = "Awareness and aim";
  On(g, "bbbHorizonScan", "Horizon scanning", "Soldiers turn their heads to scan the horizon and flanks, so they notice threats outside where they're looking.");
  On(g, "bbbWeaponReady", "Weapon back in hand", "An armed soldier walking empty-handed takes his weapon out again without waiting to be shot at.");
  On(g, "bbbCombatEngagement", "Shoot before repositioning", "A freshly visible enemy gets a chance to be shot at before the soldier moves to a new position.");
  On(g, "bbbCombatTargetPriority", "Visible targets first", "Soldiers prefer an enemy they can see over one they only know about.");
  On(g, "bbbAimFixes", "Aim fixes", "All of BBB's aim changes below. Off: vanilla aim.");
  Num(g, "bbbAimScale", 1, 0.25, 3, 0.05, "Aim error", "Overall AI aim error. 1 is vanilla; lower aims better, higher aims worse.");
  Num(g, "bbbSuppressedAim", 1.3, 1, 3, 0.05, "Aim when suppressed", "How much worse a fully suppressed soldier aims. 1 is vanilla (no effect).");
  On(g, "bbbLauncherInfantry", "Launchers on infantry", "AT gunners fire HE, thermobaric or dual-purpose rockets at groups and men in cover, and any rocket at men in buildings, unless enemy armour is about. Vanilla keeps every rocket for vehicles.");
  On(g, "bbbLauncherSwap", "Right warhead for the target", "An AT gunner reloads with the round that suits the target when he carries it: anti-tank for armour, HE for infantry when there is time.");
  On(g, "bbbLauncherCombatLoad", "Load in contact", "A gunner who carries more than one kind of rocket walks with the tube empty until he meets the enemy, then loads for the first target.");
  Num(g, "bbbLauncherAim", 0, 0, 2, 0.05, "Launcher aim error", "Rocket launchers get aim error. 0 is vanilla (launchers never miss from aim); 1 aims like a rifle.");
  Num(g, "bbbLongRangeAim", 0, 0, 5, 0.1, "Aim error at 600 m", "Aim error keeps growing past 215 m, reaching this factor at 600 m. 0 is vanilla (it stops growing at 215 m).");
  Def(g, "bbbBinocsUnderFire", BBB_ESettingKind.CHOICE, "0", "Binoculars under fire", "Soldiers being shot at or hit don't raise binoculars at distant threats.").Choices("0|1", "Kept down|Vanilla").Vanilla("1");
  Off(g, "bbbAimTuning", "Proposed aim tuning", "Sets launcher aim error 1 and aim error at 600 m 3 (not yet backed by a test).");
  On(g, "bbbExposedFire", "Fight back when pinned in the open", "A suppressed soldier with no cover keeps spotting and returns fire faster; in cover he still keeps his head down.");
  Off(g, "bbbHotAI", "Hot AI: spotting and fire timing", "Soldiers spot faster and keep their eyes when shot at, shoot sooner, and fire longer bursts with shorter aiming pauses. Off: vanilla. A Game Master can switch it in the game settings.");
  Num(g, "bbbHotFloor", 2.5, 0, 5, 0.1, "Hot AI: least spotting factor", "With spotting and fire timing on. Vanilla spots at 1 when calm and 0.4 under fire; this is the least, before the multiplier.");
  Num(g, "bbbHotSpot", 3, 0.5, 10, 0.1, "Hot AI: spotting multiplier", "With spotting and fire timing on. Times the spotting speed on top of everything else.");
  Num(g, "bbbHotWait", 0.25, 0, 1, 0.05, "Hot AI: first-shot wait", "With spotting and fire timing on. Times vanilla's wait before the first shot at a new target.");
  Num(g, "bbbHotBurst", 2, 0.5, 4, 0.1, "Hot AI: burst length", "With spotting and fire timing on. Times vanilla's bursts on full auto.");
  Num(g, "bbbHotPause", 0.4, 0.1, 1, 0.05, "Hot AI: aiming pause", "With spotting and fire timing on. Times vanilla's pause to aim between bursts and shots.");
  Off(g, "bbbHotVehicles", "Hot AI: gunners and vehicles", "Turret gunners get time to aim, hold their target, feel hits on their vehicle, fire on what they lost sight of or what their crew sees, and use the main gun; a hit armed vehicle slows down. Off: vanilla. A Game Master can switch it in the game settings.");
  Num(g, "bbbHotTurretAim", 3, 1, 10, 0.5, "Hot AI: turret aiming time", "With gunners and vehicles on. Times vanilla's time a gunner in a turret gets to bring the gun onto the target before the shot is dropped (1 s for machine guns and autocannons).");
  Num(g, "bbbHotTurretHold", 6, 0, 20, 0.5, "Hot AI: gunner holds his target (s)", "With gunners and vehicles on. A gunner in a turret keeps a target he can see this long before looking for another, unless someone else shoots at him. 0: vanilla (every 0.5 s).");
  On(g, "bbbHotVehicleHit", "Hot AI: gunners feel hits on their vehicle", "With gunners and vehicles on. A hit on an armed vehicle tells its gunners where it came from, as a hit on themselves would.");
  Num(g, "bbbHotUnderFireKmh", 20, 0, 60, 1, "Hot AI: speed under fire (km/h)", "With gunners and vehicles on. An AI-driven armed vehicle that is hit slows to this, so its gunner can shoot. 0: no slowing.");
  Num(g, "bbbHotUnderFireS", 15, 1, 60, 1, "Hot AI: slow for (s)", "With gunners and vehicles on. How long after the last hit the vehicle stays slow.");
  On(g, "bbbHotGunnerFire", "Hot AI: gunner fire control", "With gunners and vehicles on. A gunner who sees no target puts fire on the one he lost sight of, an enemy who shot at him, or what his crewmates see.");
  On(g, "bbbHotMainGun", "Hot AI: main gun on everything", "With gunners and vehicles on. A turret gunner uses the main gun (autocannon, or the machine gun in the lowest slot) against infantry and for suppression, not the coaxial MG.");
  Off(g, "bbbReturnFire", "Return fire at unseen shooters", "A soldier shot at by an enemy he can't see (behind a bush, in the dark) fires a short burst back toward where the shots came from. With fire discipline on, he gets into cover first.");
  On(g, "bbbFireDiscipline", "Fire discipline", "Shot at by an enemy they can't see, soldiers get into cover before firing back, and only two or three per squad do: riflemen within 250 m, machine gunners within 500 m, never into their last spare magazines. Suppressing a spotted group stops past 350 m (600 m for machine guns), and past 100 m bursts get shorter and aim longer. Off: the old rules.");
  On(g, "bbbSkySuppression", "No firing at aircraft noises", "Gunners don't suppress the sky because a shot was reported from an aircraft.");

  g = "Suppression";
  On(g, "bbbThreat", "Suppression changes", "All of BBB's suppression changes below. Off: vanilla suppression.");
  On(g, "bbbThreatGeometry", "Fire from above, flanks and rear", "Fire from high ground, the flanks or the rear suppresses more, and an ambush shocks a calm squad.");
  Num(g, "bbbThreatDecay", 0.55, 0.05, 5, 0.05, "Suppression wear-off speed", "How fast suppression wears off. 1 is vanilla (under 2 s); lower lasts longer.");
  Num(g, "bbbElevationThreat", 1, 0, 3, 0.1, "High-ground effect", "How much extra fire from above suppresses.");
  Num(g, "bbbFlankThreat", 0.5, 0, 2, 0.05, "Flank and rear effect", "How much extra fire into a flank or the rear suppresses.");
  Num(g, "bbbAmbushThreat", 0.2, 0, 1, 0.05, "Ambush shock", "Suppression a calm or marching squad takes when it's ambushed.");
  On(g, "bbbExplosionReflex", "Hit the dirt at explosions", "Soldiers on foot drop as the blast wave reaches them: prone close in, crouched farther out.");
  Num(g, "bbbExplosionThreat", 2.2, 0.1, 5, 0.1, "Explosion suppression", "How much a close explosion suppresses compared with vanilla.");

  g = "Squad tactics";
  On(g, "bbbSquadPicture", "Squad threat picture", "A squad under fire from two sides aims smoke, static guns and fire and manoeuvre at the group that matters most, and retreats through the open side.");
  On(g, "bbbHostIntent", "Host mod decides who fights", "A host mod says per squad whether it has ground to take, holds a post or is just travelling; squad plans and fire and manoeuvre follow that one answer. Off: the older separate rules.");
  On(g, "bbbSquadCue", "Leader points out the enemy", "A man who hasn't spotted the enemy his squad is fighting is told where it is and looks there.");
  On(g, "bbbSquadFire", "Leader splits the fire", "The squad leader puts the squad's fire on the most dangerous group and, when a second group comes from another side, sends a fireteam's fire onto it; machine gunners stay on the main threat.");
  On(g, "bbbSquadJobs", "Leader picks who helps a casualty", "In a fight the machine gunner, the leader, the AT gunner (with enemy armour about) and a short-handed flank team stay on their job; someone else goes to the casualty, or to a static gun.");
  On(g, "bbbSquadTeams", "Leader draws the bounding teams", "Fire and manoeuvre and the flank and search drills move in the leader's halves: left and right of the line to the enemy, gunner and assistant together, the machine gunner's half covering first; a fireteam on the flank keeps its sector.");
  On(g, "bbbTactic", "Squad plans", "A short pause on first contact, then assault when the odds are fair, or hold and fight from cover when outnumbered, flanked or pinned.");
  On(g, "bbbTacticFlank", "Fix and flank", "A squad held by a dug-in enemy fixes it and sends a team round the side.");
  On(g, "bbbTacticSearch", "Search after losing contact", "A squad that loses contact searches where the enemy was last seen.");
  Off(g, "bbbTacticFire", "Base of fire while holding", "A holding squad puts fire on the enemy from cover (no measured gain yet).");
  Num(g, "bbbTacticAssault", 85, 30, 300, 5, "Assault lasts (s)", "How long a squad keeps assaulting before it judges the fight again.");
  Num(g, "bbbTacticHold", 45, 15, 180, 5, "Hold lasts (s)", "How long a squad holds and fights from cover before it judges the fight again.");
  Num(g, "bbbTacticReorg", 20, 5, 60, 1, "Reorganise after losing the leader (s)", "How long a squad holds while a new leader takes over.");
  On(g, "bbbFireManeuver", "Fire and manoeuvre", "Squads advance in alternating bounds while the other half covers.");
  On(g, "bbbFireCoverOrders", "Ordered covering fire", "The covering half is ordered to fire at the last reported enemy position.");
  On(g, "bbbCoverFire", "Covering fire at cover", "Covering fire aims at the cover nearest a reported enemy.");
  On(g, "bbbCombatManeuver", "Combat repositioning", "Soldiers in contact move to better positions.");
  On(g, "bbbCombatDetail", "Keep nearby fights detailed", "Groups near the enemy stay fully simulated while in contact.");
  On(g, "bbbCombatRadio", "Contact reports in the group", "A group shares the positions its members saw.");
  On(g, "bbbCombatResponse", "Respond to incoming fire", "A group reacts toward where incoming fire came from.");
  On(g, "bbbContactRelay", "Contact sharing between squads", "Squads pass on enemies they spotted by radio (personal or manpack range), or by voice without a radio.");
  Num(g, "bbbRelayVoice", 60, 0, 150, 5, "Voice range (m)", "How far a squad without a radio can call out a contact. 0: radio only.");
  Num(g, "bbbRelayFresh", 8, 2, 30, 1, "Only fresh sightings (s)", "How recent a sighting must be to be passed on.");
  On(g, "bbbBuildings", "Use buildings", "Soldiers in contact move into nearby buildings for cover and clear them.");
  On(g, "bbbIndoorHold", "Hold inside", "Soldiers in a building, and squads holding one, aren't run outside by every gunshot.");
  On(g, "bbbDoorUnstick", "Open blocking doors", "A soldier stuck at a house door opens or swings it.");

  g = "Grenades, smoke and flares";
  On(g, "bbbGrenades", "Hand grenades", "Soldiers throw fragmentation grenades at enemies in cover within throwing range, never near friends.");
  Num(g, "bbbGrenadesMinDist", 12, 8, 25, 1, "Grenade minimum range (m)", "Closest target a grenade is thrown at.");
  Num(g, "bbbGrenadesMaxDist", 38, 20, 45, 1, "Grenade maximum range (m)", "Farthest target a grenade is thrown at.");
  Num(g, "bbbGrenadesFriendlyDist", 14, 8, 25, 1, "Grenade friendly safety (m)", "No grenade lands closer than this to a friendly soldier.");
  Num(g, "bbbGrenadesCooldown", 25, 5, 120, 1, "Grenade squad cooldown (s)", "Time between grenades from the same squad.");
  Num(g, "bbbGrenadesCorridor", 2.5, 1, 5, 0.25, "Grenade throwing lane (m)", "No grenade is thrown if a friendly is this close to its flight path.");
  On(g, "bbbSmoke", "Tactical smoke", "Soldiers throw smoke toward the enemy to screen casualty drags, bounds and withdrawals.");
  On(g, "bbbSmokeDrag", "Smoke for drags", "Smoke screens a casualty being dragged.");
  On(g, "bbbSmokeManeuver", "Smoke for bounds", "Smoke screens a bound in fire and manoeuvre.");
  On(g, "bbbSmokeMorale", "Smoke for withdrawals", "Smoke screens a squad falling back.");
  Num(g, "bbbSmokeCooldown", 30, 5, 180, 1, "Smoke squad cooldown (s)", "Time between smoke grenades from the same squad.");
  Num(g, "bbbSmokeTargetCooldown", 45, 10, 180, 5, "Smoke on one spot cooldown (s)", "Time before smoke is thrown at the same place again.");
  Num(g, "bbbSmokeDist", 20, 8, 35, 1, "Smoke throw distance (m)", "How far toward the enemy smoke is thrown.");
  On(g, "bbbFlares", "Night illumination", "At night, squads in contact fire illumination when they have it.");
  Num(g, "bbbFlaresSquadCooldown", 120, 30, 600, 10, "Illumination squad cooldown (s)", "Time between illumination rounds from the same squad.");
  Num(g, "bbbFlaresAreaCooldown", 90, 30, 600, 10, "Illumination area cooldown (s)", "Time before anyone lights up the same area again.");

  g = "Morale";
  On(g, "bbbMorale", "Morale and break contact", "Badly hurt or pinned squads fall back in alternating halves.");
  On(g, "bbbMoraleSurrender", "Surrender", "A soldier cut off from his squad, pinned, out of spare ammo or badly wounded, with an enemy close, may drop his weapons and give up. AI leave him alone.");
  On(g, "bbbMoraleSurrenderFates", "Surrenders can go wrong", "Now and then captors shoot a prisoner, or a prisoner who hid a grenade or pistol uses it on whoever comes close.");
  Num(g, "bbbMoraleCasualties", 0.5, 0.15, 0.9, 0.05, "Losses before breaking", "Share of the squad lost that counts fully toward a break.");
  Num(g, "bbbMoraleSuppression", 0.6, 0.15, 1, 0.05, "Suppression before breaking", "Suppression that counts fully toward a break.");
  Num(g, "bbbMoraleDistance", 25, 10, 50, 1, "Withdrawal bound (m)", "Length of each bound when a squad falls back.");
  Num(g, "bbbMoraleBreak", 0.7, 0.3, 1, 0.05, "Breaking point", "Morale score (losses, suppression, leader lost) at which a squad breaks. Higher: squads hold out longer.");
  Num(g, "bbbMoraleWithdraw", 90, 30, 250, 5, "Withdrawal distance (m)", "How far a broken squad falls back in all.");
  On(g, "bbbWithdrawOrder", "Covered fall-back on orders", "A squad its commander pulls back while under fire bounds back in covering halves instead of going to ground. Only when a host mod (AC: Actual) asks; does nothing on its own.");
  Num(g, "bbbWithdrawOrderBound", 40, 20, 60, 1, "Fall-back bound (m)", "Length of each bound when a squad falls back on orders.");

  g = "Medical and ammo";
  On(g, "bbbMedic", "Safer healing", "Medics and buddies don't run through fire to a casualty and heal from cover.");
  On(g, "bbbMedicSmoke", "Smoke before aid", "Smoke goes out toward the threat before anyone moves to an exposed casualty.");
  On(g, "bbbDrag", "Casualty drags", "Casualties in the open are dragged to cover.");
  On(g, "bbbDragDeep", "Drag well into cover", "A dragged casualty is taken a few metres past the edge of the cover, so he isn't left half in view.");
  On(g, "bbbRearm", "Rearm from the fallen", "Soldiers low on ammo take magazines and launchers from nearby friendly bodies, even mid-fight.");
  On(g, "bbbSpecialistTeams", "Specialist teams", "Machine gunners and AT gunners keep the assistant who carries their ammunition close; he hands over ammo before the gun runs dry and takes the launcher if the gunner falls.");
  Num(g, "bbbRearmRadius", 25, 5, 100, 1, "Rearm search radius (m)", "How far a soldier goes for ammo.");

  g = "Static weapons";
  On(g, "bbbStaticWeapons", "Crew static weapons", "Soldiers crew a nearby loaded static gun when the enemy is in its arc.");
  Num(g, "bbbStaticReach", 35, 10, 80, 1, "Static gun search radius (m)", "How far a soldier goes to crew a static gun.");
  Num(g, "bbbStaticArc", 60, 15, 180, 5, "Static gun arc (degrees)", "How far off the gun's facing the enemy can be.");
  Num(g, "bbbStaticQuiet", 15, 5, 120, 1, "Leave the gun after quiet (s)", "How long a gunner waits after the last contact before leaving the gun.");
  On(g, "bbbHoldGuns", "Held guns", "Gunners stay on static guns a host mod marks as held.");
  On(g, "bbbGunWinchester", "Off a dry gun", "A gunner whose static gun is out of ammo gets off and fights on foot.");

  g = "Vehicles and crews";
  On(g, "bbbArmour", "Armour learning", "Soldiers learn which rounds can't hurt which vehicles and stop wasting ammo on them.");
  Num(g, "bbbArmourHits", 12, 1, 40, 1, "Rounds before learning", "Rounds of one kind on one kind of vehicle before soldiers judge them useless.");
  On(g, "bbbArmourScatter", "Get low near armour", "Soldiers without anti-armour weapons crouch or go prone near enemy armour; AT soldiers hold to engage.");
  On(g, "bbbArmorReaction", "Armour reaction to AT fire", "Vehicles fire smoke launchers and slow wheeled vehicles back out of a launcher's line of fire.");
  On(g, "bbbGunnerDrill", "Gunner contact drill", "A vehicle gunner shot at by an enemy he can't see fires short bursts toward the sound.");
  On(g, "bbbGunnerScan", "Gunner scan on the move", "Gunners in a moving vehicle scan 75 degrees each side instead of 15.");
  On(g, "bbbCrewLeader", "Crew leader", "The vehicle commander (else the driver) builds one picture from everything his crew, riders and nearby squadmates see, gives the gunner the most dangerous group, switches him onto it or breaks off a lesser fight to lay him on it, and calls the target out.");
  On(g, "bbbCrewCallout", "Crew call-out", "A vehicle gunner who sees nothing of his own turns onto the biggest group of enemies his crewmates see and puts fire on it until he spots them himself.");
  On(g, "bbbGunnerOrders", "Gunners fire on the move", "A move order sends the vehicle, not the gunner: under a forced move (AC: Actual's fast moves) a vehicle gunner still shoots what he sees or his crew calls out, and BBB's gunner help still works. Hold fire still holds him. Off: vanilla, where the move outranks his fire.");
  On(g, "bbbGunnerReplace", "Replace lost gunners", "Squadmates take over an empty or wounded gunner seat on a working armed vehicle.");
  On(g, "bbbCommanderCrew", "Commander seats are crew", "Turret-linked commander positions count as crew.");
  On(g, "bbbCrewStays", "Crew stay mounted", "Crews of armed or armoured vehicles don't get out to open gates or heal.");
  On(g, "bbbCrewTurnout", "Crew turn out", "Crews turn out in quiet periods and button up in danger.");
  On(g, "bbbDriverAlign", "Driver alignment", "An idle driver turns the vehicle slightly so the gunner can see his target.");
  On(g, "bbbTacticalDismount", "Dismount under fire", "Passengers of a vehicle slowing under fire get out to fight; turret crews stay.");
  On(g, "bbbDoors", "Gates and doors", "AI drivers open gates ahead without stopping; crews close doors and hatches once aboard.");
  On(g, "bbbUnstick", "Unstick vehicles", "Stuck AI drivers reverse and steer away; soldiers left standing on a truck bed get down.");
  Off(g, "bbbGiveWay", "Vehicles give way", "Friendly vehicles on a collision course agree who gives way: head-on, one pulls over to its right and waits; anywhere else it brakes. Never for an enemy.");
  On(g, "bbbHandbrake", "Handbrake when the driver leaves", "A vehicle its AI driver has left gets its handbrake on (sometimes forgotten under fire). From Reforger 1.9 an empty vehicle drops into neutral and rolls away on a slope.");
  Off(g, "bbbEvadeArmour", "Unarmed vehicles get away from armed enemies", "An unarmed vehicle whose crew sees an armed enemy vehicle asks the mission to drive it away.");

  g = "Movement";
  On(g, "bbbRelaxedMarch", "Relaxed march", "On ordinary travel soldiers lower their weapons (never holster them).");
  On(g, "bbbShoreRoutes", "Shore routes", "Foot routes round water keep off low shore ground.");
  On(g, "bbbTrueCover", "Cover must face the enemy", "A soldier only counts as in cover when the cover is between him and the enemy he knows about; one on the wrong side of a wall moves behind it or fights from the open.");
  On(g, "bbbRoadExit", "Off the road in a firefight", "A soldier caught on a road in a firefight moves off it, into cover beside it or onto the verge, instead of lying down on the open road.");

  g = "Vanilla AI";
  Def(g, "bbbAISkill", BBB_ESettingKind.CHOICE, "0", "Skill", "The skill every AI soldier spawns with (in vanilla it changes aim). A host mod, Game Master or mission that sets a soldier's skill still wins.").Choices("0|10|20|50|70|80|100", "Mission's own|Novice|Rookie|Regular|Veteran|Expert|Perfect");
  Num(g, "bbbAIPerception", 1, 0.25, 3, 0.05, "Spotting speed", "How fast AI soldiers spot what they see. 1 is vanilla. A host mod or Game Master that sets it for a soldier still wins.");

  g = "Diagnostics";
  Off(g, "bbbHotTrace", "Hot AI: gunner trace in the log", "Every 2 s, a log line per AI vehicle gunner in contact: what he is doing, his target, gun and bursts.");
  Def(g, "bbbThink", BBB_ESettingKind.CHOICE, "0", "Think panel", "Look at a soldier to see BBB's decisions and its reasons for holding back. For hosts and mission makers.").Choices("0|1|2", "Off|On|On, with log notes");
  Off(g, "bbbTacticStats", "Squad plan stats in the log", "Logs how far each squad plan really moved the squad, and a minute-by-minute count of squads in contact and why plans were left out. For testing.");
 }

 // ---- small helpers ----

 // 12, 0.55, 2.2: at most two decimals, no trailing zeros.
 static string Number(float value)
 {
  int hundredths = Math.Round(value * 100);
  string sign = "";
  if (hundredths < 0)
  {
   sign = "-";
   hundredths = -hundredths;
  }
  int whole = hundredths / 100;
  int part = hundredths % 100;
  if (part == 0) return sign + whole.ToString();
  if (part % 10 == 0) return sign + whole.ToString() + "." + (part / 10).ToString();
  if (part < 10) return sign + whole.ToString() + ".0" + part.ToString();
  return sign + whole.ToString() + "." + part.ToString();
 }

 static string ActiveName()
 {
  array<string> lines = SCR_FileIOHelper.ReadFileContent(ACTIVE_FILE, false);
  if (!lines) return "";
  foreach (string line : lines)
  {
   line = line.Trim();
   if (!line.IsEmpty()) return line;
  }
  return "";
 }

 static void SetActiveName(string name)
 {
  FileIO.MakeDirectory("$profile:BBB");
  SCR_FileIOHelper.WriteFileContent(ACTIVE_FILE, {name});
 }
}

// A preset: a name and the values it changes. Built-in presets live in code and can't be edited.
class BBB_Preset
{
 string m_sName;
 string m_sFile;
 bool m_bBuiltIn;
 ref map<string, string> m_Values = new map<string, string>();

 string Value(BBB_Setting setting)
 {
  string value;
  if (m_Values.Find(setting.m_sKey, value)) return setting.Clean(value);
  return setting.m_sDefault;
 }

 void SetValue(BBB_Setting setting, string value)
 {
  value = setting.Clean(value);
  if (value == setting.m_sDefault) m_Values.Remove(setting.m_sKey);
  else m_Values.Set(setting.m_sKey, value);
 }

 static array<ref BBB_Preset> BuiltIn()
 {
  array<ref BBB_Preset> presets = {};
  BBB_Preset preset = new BBB_Preset();
  preset.m_sName = BBB_Settings.DEFAULT_PRESET;
  preset.m_bBuiltIn = true;
  presets.Insert(preset);
  preset = new BBB_Preset();
  preset.m_sName = BBB_Settings.VANILLA_PRESET;
  preset.m_bBuiltIn = true;
  foreach (BBB_Setting setting : BBB_Settings.All())
  {
   if (!setting.m_sVanilla.IsEmpty()) preset.m_Values.Set(setting.m_sKey, setting.m_sVanilla);
  }
  presets.Insert(preset);
  return presets;
 }

 // Built-in presets first, then the player's files by name.
 static array<ref BBB_Preset> List()
 {
  array<ref BBB_Preset> presets = BuiltIn();
  array<string> files = SCR_FileIOHelper.FindFiles(BBB_Settings.DIR, ".json");
  if (!files) return presets;
  array<ref BBB_Preset> own = {};
  foreach (string file : files)
  {
   BBB_Preset preset = ReadFile(file);
   if (!preset) continue;
   bool taken;
   foreach (BBB_Preset other : presets)
   {
    if (other.m_sName == preset.m_sName) taken = true;
   }
   foreach (BBB_Preset other : own)
   {
    if (other.m_sName == preset.m_sName) taken = true;
   }
   if (taken) continue;
   int at = 0;
   while (at < own.Count() && own[at].m_sName.Compare(preset.m_sName, false) < 0) at++;
   own.InsertAt(preset, at);
  }
  foreach (BBB_Preset preset : own) presets.Insert(preset);
  return presets;
 }

 // By its name or its file name (with or without .json).
 static BBB_Preset Find(string name)
 {
  name = name.Trim();
  string lower = name;
  lower.ToLower();
  array<ref BBB_Preset> presets = List();
  foreach (BBB_Preset preset : presets)
  {
   string presetName = preset.m_sName;
   presetName.ToLower();
   if (presetName == lower) return preset;
   string file = FileName(preset.m_sName);
   file.ToLower();
   if (file == lower || file + ".json" == lower) return preset;
  }
  return null;
 }

 static string FileName(string name)
 {
  string file = "";
  for (int i = 0; i < name.Length(); i++)
  {
   string c = name.Get(i);
   int code = c.ToAscii();
   bool keep = (code >= 48 && code <= 57) || (code >= 65 && code <= 90) || (code >= 97 && code <= 122) || c == "-" || c == "_";
   if (keep) file += c;
   else if (c == " ") file += "_";
  }
  if (file.IsEmpty()) file = "preset";
  return file;
 }

 // A name no other preset has: "Copy of X", "Copy of X 2", ...
 static string FreeName(string wanted)
 {
  string name = wanted;
  int n = 2;
  while (Find(name) || FileIO.FileExists(BBB_Settings.DIR + "/" + FileName(name) + ".json"))
  {
   name = wanted + " " + n.ToString();
   n++;
  }
  return name;
 }

 BBB_Preset Copy(string name)
 {
  BBB_Preset copy = new BBB_Preset();
  copy.m_sName = name;
  foreach (string key, string value : m_Values) copy.m_Values.Set(key, value);
  return copy;
 }

 bool Save()
 {
  if (m_bBuiltIn) return false;
  FileIO.MakeDirectory("$profile:BBB");
  FileIO.MakeDirectory(BBB_Settings.DIR);
  string file = BBB_Settings.DIR + "/" + FileName(m_sName) + ".json";
  if (!m_sFile.IsEmpty() && m_sFile != file) FileIO.DeleteFile(m_sFile);
  m_sFile = file;
  array<string> lines = {};
  ToJson(lines);
  return SCR_FileIOHelper.WriteFileContent(file, lines);
 }

 void Delete()
 {
  if (!m_bBuiltIn && !m_sFile.IsEmpty()) FileIO.DeleteFile(m_sFile);
 }

 // Every value this preset sets, in the Settings tab's order; built-in presets write out
 // everything they change so a server gets exactly what the player saw.
 void ToJson(notnull array<string> lines)
 {
  lines.Insert("{");
  lines.Insert("  \"name\": \"" + Escape(m_sName) + "\",");
  lines.Insert("  \"values\": {");
  array<string> pairs = {};
  foreach (BBB_Setting setting : BBB_Settings.All())
  {
   string value;
   if (m_Values.Find(setting.m_sKey, value)) pairs.Insert("    \"" + setting.m_sKey + "\": \"" + Escape(setting.Clean(value)) + "\"");
  }
  for (int i = 0; i < pairs.Count(); i++)
  {
   if (i < pairs.Count() - 1) lines.Insert(pairs[i] + ",");
   else lines.Insert(pairs[i]);
  }
  lines.Insert("  }");
  lines.Insert("}");
 }

 string Json()
 {
  array<string> lines = {};
  ToJson(lines);
  string text = "";
  foreach (string line : lines) text += line + "\n";
  return text;
 }

 static BBB_Preset ReadFile(string file)
 {
  array<string> lines = SCR_FileIOHelper.ReadFileContent(file, false);
  if (!lines) return null;
  string text = "";
  foreach (string line : lines) text += line + "\n";
  BBB_Preset preset = Parse(text);
  if (!preset)
  {
   PrintFormat("[BBB] SETTINGS %1 is not a BBB preset (expected {\"name\": ..., \"values\": {...}})", file, level: LogLevel.WARNING);
   return null;
  }
  preset.m_sFile = file;
  if (preset.m_sName.IsEmpty())
  {
   // A file without a name is named after itself.
   string name = FilePath.StripExtension(FilePath.StripPath(file));
   preset.m_sName = name;
  }
  return preset;
 }

 // The flat JSON this class writes: {"name": "...", "values": {"key": "value" or number, ...}}.
 // Unknown keys are kept (a newer BBB's options survive an older one's save); unknown top-level
 // fields are skipped.
 static BBB_Preset Parse(string text)
 {
  array<string> tokens = {};
  if (!Tokens(text, tokens)) return null;
  if (tokens.Count() < 2 || tokens[0] != "{") return null;
  BBB_Preset preset = new BBB_Preset();
  int i = 1;
  bool values;
  while (i < tokens.Count())
  {
   string token = tokens[i];
   if (token == "}")
   {
    if (!values) return preset;
    values = false;
    i++;
    continue;
   }
   if (token == ",")
   {
    i++;
    continue;
   }
   if (i + 2 >= tokens.Count() || tokens[i + 1] != ":") return null;
   string key = Unquote(token);
   string value = tokens[i + 2];
   if (values)
   {
    preset.m_Values.Set(key, Unquote(value));
    i += 3;
    continue;
   }
   if (key == "values" && value == "{")
   {
    values = true;
    i += 3;
    continue;
   }
   if (key == "name") preset.m_sName = Unquote(value).Trim();
   if (value == "{" || value == "[") return null;
   i += 3;
  }
  return null;
 }

 // Strings keep their quotes (and escapes resolved) so a value can't pass for punctuation.
 protected static bool Tokens(string text, notnull array<string> tokens)
 {
  int n = text.Length();
  int i = 0;
  while (i < n)
  {
   string c = text.Get(i);
   if (c == " " || c == "\n" || c == "\r" || c == "\t")
   {
    i++;
    continue;
   }
   if (c == "{" || c == "}" || c == ":" || c == "," || c == "[" || c == "]")
   {
    tokens.Insert(c);
    i++;
    continue;
   }
   if (c == "\"")
   {
    string s = "\"";
    i++;
    bool closed;
    while (i < n)
    {
     string d = text.Get(i);
     if (d == "\\" && i + 1 < n)
     {
      string e = text.Get(i + 1);
      if (e == "n") s += "\n";
      else s += e;
      i += 2;
      continue;
     }
     i++;
     if (d == "\"")
     {
      closed = true;
      break;
     }
     s += d;
    }
    if (!closed) return false;
    tokens.Insert(s);
    continue;
   }
   // A bare number, true or false.
   string word = "";
   while (i < n)
   {
    string d = text.Get(i);
    if (d == " " || d == "\n" || d == "\r" || d == "\t" || d == "," || d == "}" || d == "]" || d == ":") break;
    word += d;
    i++;
   }
   if (word == "true") word = "1";
   if (word == "false") word = "0";
   tokens.Insert(word);
  }
  return true;
 }

 protected static string Unquote(string token)
 {
  if (token.StartsWith("\"")) return token.Substring(1, token.Length() - 1);
  return token;
 }

 protected static string Escape(string value)
 {
  value.Replace("\\", "\\\\");
  value.Replace("\"", "\\\"");
  return value;
 }
}

modded class ArmaReforgerScripted
{
 // Runs when the game object is created, before any world or entity: BBB's statics exist from
 // the first frame. Modded constructors chain, so a host mod's own constructor still runs.
 void ArmaReforgerScripted()
 {
  BBB_StaticInit.Run();
 }

 protected override void OnBeforeWorldCleanup()
 {
  // Stop first: Stop hands state back with this mission's settings still read
  BBB_Vanilla.OnWorldCleanup();
  BBB_Settings.Invalidate();
  super.OnBeforeWorldCleanup();
 }
}
