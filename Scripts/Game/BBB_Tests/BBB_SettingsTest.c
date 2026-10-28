// Settings and presets (BBB #8). Runs with -bbbSettingsTest 1 -bbbStaticArc 120. Checks what a
// mission actually runs with, read back from the features' own fields after each load:
//  1. registry: unique keys, defaults inside their ranges, defaults equal the fields' shipped values;
//  2. a preset file written to $profile:BBB/Presets is applied, and a launch parameter beats it;
//  3. the built-in Vanilla preset turns features off;
//  4. the next mission without a preset gets the defaults back (statics outlive a mission);
//  5. JSON round trip of an awkward name and every kind of value; a broken file is refused;
//  6. the Settings tab layout resource loads.
// What it doesn't establish: anything on screen (the tab is checked by eye in a client), or
// AI behaviour (no behaviour changes here; each feature's own suite covers its values).
class BBB_SettingsTest
{
 static ref BBB_SettingsTest s_Test;
 protected int m_iChecks;
 protected int m_iFailures;
 static const int EXPECTED = 65;
 static const string FILE = "$profile:BBB/Presets/BBB_settings_test.json";

 static void MaybeStart()
 {
  if (s_Test || !Replication.IsServer() || !System.IsCLIParam("bbbSettingsTest")) return;
  s_Test = new BBB_SettingsTest();
  GetGame().GetCallqueue().CallLater(s_Test.Run, 3000);
 }

 protected void Check(string name, bool ok, string detail = "")
 {
  m_iChecks++;
  string result = "PASS";
  if (!ok)
  {
   result = "FAIL";
   m_iFailures++;
  }
  PrintFormat("[BBB] SETTINGSTEST %1 %2 %3", result, name, detail);
 }

 protected void Near(string name, float actual, float expected)
 {
  Check(name, Math.AbsFloat(actual - expected) < 0.001, string.Format("actual=%1 expected=%2", actual, expected));
 }

 // A new mission: settings reload, every feature reads again.
 protected void Mission(string preset)
 {
  BBB_Settings.s_bTestPick = true;
  BBB_Settings.s_sTestPreset = preset;
  BBB_Settings.Invalidate();
  BBB_Grenades.Read();
  BBB_Smoke.Read();
  BBB_StaticWeapons.Read();
  BBB_Tactic.Read();
  BBB_Morale.Read();
  BBB_Threat.Read();
  BBB_Aim.Read();
  BBB_FireManeuver.Read();
  BBB_Rearm.Read();
  BBB_Buildings.Read();
  BBB_ContactRelay.Read();
  BBB_Flares.Read();
 }

 // A rifleman spawned now: the skill and spotting factor his combat component starts with.
 protected void Soldier(out int skill, out float perception)
 {
  skill = -1;
  perception = -1;
  vector at = BBB.Ground("1000 0 1000");
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  params.Transform[3] = at;
  IEntity man = GetGame().SpawnEntityPrefab(Resource.Load(BBB_AimTest.RIFLE_US), GetGame().GetWorld(), params);
  if (!man) return;
  SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(man.FindComponent(SCR_AICombatComponent));
  if (combat)
  {
   skill = combat.GetAISkill();
   perception = combat.GetPerceptionFactor();
  }
  SCR_EntityHelper.DeleteEntityAndChildren(man);
 }

 protected void Run()
 {
  // 1. Registry.
  array<string> keys = {};
  bool unique = true;
  bool inRange = true;
  bool clean = true;
  foreach (BBB_Setting setting : BBB_Settings.All())
  {
   if (keys.Contains(setting.m_sKey)) unique = false;
   keys.Insert(setting.m_sKey);
   if (setting.Clean(setting.m_sDefault) != setting.m_sDefault)
   {
    clean = false;
    PrintFormat("[BBB] SETTINGSTEST default of %1 changes when cleaned: %2", setting.m_sKey, setting.m_sDefault);
   }
   if (setting.m_eKind == BBB_ESettingKind.NUMBER && (setting.m_sDefault.ToFloat() < setting.m_fMin || setting.m_sDefault.ToFloat() > setting.m_fMax)) inRange = false;
  }
  Check("registry-size", keys.Count() >= 86, keys.Count().ToString());
  Check("registry-unique", unique);
  Check("registry-defaults-clean", clean);
  Check("registry-defaults-in-range", inRange);

  // Defaults equal the fields' shipped values (no preset, nothing on the command line but the arc).
  Mission("");
  Near("default-grenade-min", BBB_Grenades.s_fMinThrowDistM, BBB_Settings.Find("bbbGrenadesMinDist").m_sDefault.ToFloat());
  Near("default-grenade-cooldown", BBB_Grenades.s_fSquadCooldownS, BBB_Settings.Find("bbbGrenadesCooldown").m_sDefault.ToFloat());
  Near("default-smoke-cooldown", BBB_Smoke.s_fCooldownSquadS, BBB_Settings.Find("bbbSmokeCooldown").m_sDefault.ToFloat());
  Near("default-static-reach", BBB_StaticWeapons.s_fSearchRadiusM, BBB_Settings.Find("bbbStaticReach").m_sDefault.ToFloat());
  Near("default-morale-casualties", BBB_Morale.s_fCasualtyThreshold, BBB_Settings.Find("bbbMoraleCasualties").m_sDefault.ToFloat());
  Near("default-threat-decay", BBB_Threat.s_fDecayScale, BBB_Settings.Find("bbbThreatDecay").m_sDefault.ToFloat());
  Near("default-aim-suppressed", BBB_Aim.s_fSuppressed, BBB_Settings.Find("bbbSuppressedAim").m_sDefault.ToFloat());
  Near("default-rearm-radius", BBB_Rearm.s_fSearchRadius, BBB_Settings.Find("bbbRearmRadius").m_sDefault.ToFloat());
  Check("default-tactic-fire-off", !BBB_Tactic.s_bFire);
  Check("default-smoke-on", BBB_Smoke.s_bOn);
  Near("cli-arc-without-preset", BBB_StaticWeapons.s_fMaxArcDeg, 120);
  int skill0;
  float perception0;
  Soldier(skill0, perception0);
  Check("spawn-default-skill-is-prefabs", skill0 > 0, skill0.ToString());
  Near("spawn-default-perception", perception0, 1);

  // 2. A preset file.
  BBB_Preset preset = new BBB_Preset();
  preset.m_sName = "BBB settings test";
  preset.SetValue(BBB_Settings.Find("bbbGrenadesMinDist"), "20");
  preset.SetValue(BBB_Settings.Find("bbbSmoke"), "0");
  preset.SetValue(BBB_Settings.Find("bbbTacticFire"), "1");
  preset.SetValue(BBB_Settings.Find("bbbStaticArc"), "90");
  preset.SetValue(BBB_Settings.Find("bbbThreatDecay"), "0.3");
  preset.SetValue(BBB_Settings.Find("bbbAimTuning"), "1");
  preset.SetValue(BBB_Settings.Find("bbbRearmRadius"), "500");
  preset.SetValue(BBB_Settings.Find("bbbTacticHold"), "60");
  preset.SetValue(BBB_Settings.Find("bbbRelayVoice"), "0");
  preset.SetValue(BBB_Settings.Find("bbbMoraleBreak"), "0.9");
  preset.SetValue(BBB_Settings.Find("bbbFlaresSquadCooldown"), "300");
  preset.SetValue(BBB_Settings.Find("bbbAISkill"), "80");
  preset.SetValue(BBB_Settings.Find("bbbAIPerception"), "2");
  Check("preset-default-not-stored", preset.m_Values.Count() == 13, preset.m_Values.Count().ToString());
  Check("preset-saved", preset.Save() && FileIO.FileExists(FILE), preset.m_sFile);
  Check("preset-listed", BBB_Preset.Find("bbb_settings_test") != null);
  Mission("BBB settings test");
  Check("preset-active", BBB_Settings.ActivePreset() == "BBB settings test", BBB_Settings.ActivePreset());
  Near("preset-grenade-min", BBB_Grenades.s_fMinThrowDistM, 20);
  Check("preset-smoke-off", !BBB_Smoke.s_bOn);
  Check("preset-tactic-fire-on", BBB_Tactic.s_bFire);
  Near("preset-threat-decay", BBB_Threat.s_fDecayScale, 0.3);
  Near("preset-aim-tuning-launcher", BBB_Aim.s_fLauncher, BBB_Aim.TUNED_LAUNCHER);
  Near("preset-out-of-range-clamped", BBB_Rearm.s_fSearchRadius, 100);
  Near("cli-beats-preset", BBB_StaticWeapons.s_fMaxArcDeg, 120);
  Near("untouched-keeps-default", BBB_Grenades.s_fSquadCooldownS, 25);
  Near("preset-tactic-hold", BBB_Tactic.s_fHoldS, 60);
  Near("preset-relay-voice", BBB_ContactRelay.s_fVoiceM, 0);
  Near("preset-morale-break", BBB_Morale.s_fBreakScore, 0.9);
  Near("preset-flares-cooldown", BBB_Flares.s_fSquadCooldownMs, 300000);
  int skill;
  float perception;
  Soldier(skill, perception);
  Check("spawn-preset-skill", skill == EAISkill.EXPERT, skill.ToString());
  Near("spawn-preset-perception", perception, 2);

  // 3. Vanilla.
  Mission(BBB_Settings.VANILLA_PRESET);
  Check("vanilla-grenades-off", !BBB_Grenades.s_bOn);
  Check("vanilla-smoke-off", !BBB_Smoke.s_bOn);
  Check("vanilla-tactic-off", !BBB_Tactic.s_bOn);
  Check("vanilla-morale-off", !BBB_Morale.s_bOn);
  Check("vanilla-threat-off", !BBB_Threat.s_bOn);
  Check("vanilla-aim-off", !BBB_Aim.s_bOn);
  Check("vanilla-binoculars", !BBB_Aim.s_bBinoculars);
  Check("vanilla-fire-maneuver-off", !BBB_FireManeuver.s_bOn);
  Check("vanilla-buildings-off", !BBB_Buildings.s_bOn && !BBB_Buildings.s_bHold);
  bool allOff = true;
  BBB_Preset plain = BBB_Preset.Find(BBB_Settings.VANILLA_PRESET);
  foreach (BBB_Setting setting : BBB_Settings.All())
  {
   if (setting.m_eKind == BBB_ESettingKind.BOOL && setting.m_sDefault == "1" && plain.Value(setting) != "0") allOff = false;
  }
  Check("vanilla-every-switch-off", allOff);

  // 4. The next mission without a preset: the values the earlier presets changed come back.
  Mission("");
  Check("next-mission-no-preset", BBB_Settings.ActivePreset().IsEmpty());
  Near("restored-grenade-min", BBB_Grenades.s_fMinThrowDistM, 12);
  Check("restored-grenades-on", BBB_Grenades.s_bOn);
  Check("restored-smoke-on", BBB_Smoke.s_bOn);
  Check("restored-tactic-fire-off", !BBB_Tactic.s_bFire);
  Near("restored-threat-decay", BBB_Threat.s_fDecayScale, 0.55);
  Near("restored-aim-launcher", BBB_Aim.s_fLauncher, 0);
  Check("restored-binoculars", BBB_Aim.s_bBinoculars);
  Near("restored-tactic-hold", BBB_Tactic.s_fHoldS, 45);
  Near("restored-relay-voice", BBB_ContactRelay.s_fVoiceM, 60);
  Near("restored-morale-break", BBB_Morale.s_fBreakScore, 0.7);
  Soldier(skill, perception);
  Check("spawn-restored-skill", skill == skill0, skill.ToString());
  Near("spawn-restored-perception", perception, 1);

  // 5. JSON.
  BBB_Preset odd = new BBB_Preset();
  odd.m_sName = "Jacob's \"hard\" \\ preset";
  odd.SetValue(BBB_Settings.Find("bbbThink"), "2");
  odd.SetValue(BBB_Settings.Find("bbbBinocsUnderFire"), "1");
  odd.SetValue(BBB_Settings.Find("bbbMoraleCasualties"), "0.35");
  odd.SetValue(BBB_Settings.Find("bbbFlares"), "0");
  BBB_Preset back = BBB_Preset.Parse(odd.Json());
  Check("json-parsed", back != null);
  if (back)
  {
   Check("json-name", back.m_sName == odd.m_sName, back.m_sName);
   Check("json-values", back.m_Values.Count() == 4 && back.Value(BBB_Settings.Find("bbbThink")) == "2" && back.Value(BBB_Settings.Find("bbbMoraleCasualties")) == "0.35" && back.Value(BBB_Settings.Find("bbbFlares")) == "0");
  }
  BBB_Preset bare = BBB_Preset.Parse("{\"values\": {\"bbbGrenadesMinDist\": 15, \"bbbSmoke\": false, \"bbbFromTheFuture\": \"x\"}, \"comment\": \"hand written\"}");
  Check("json-bare-values", bare && bare.Value(BBB_Settings.Find("bbbGrenadesMinDist")) == "15" && bare.Value(BBB_Settings.Find("bbbSmoke")) == "0" && bare.m_Values.Contains("bbbFromTheFuture"));
  Check("json-broken-refused", BBB_Preset.Parse("{\"name\": \"x\", \"values\": {\"bbbSmoke\": ") == null);

  // 6. The tab's layout.
  Resource layout = Resource.Load(SCR_SettingsSuperMenu.BBB_TAB);
  Check("tab-layout-loads", layout && layout.IsValid());

  preset.Delete();
  Check("preset-deleted", !FileIO.FileExists(FILE));
  BBB_Settings.s_bTestPick = false;
  BBB_Settings.Invalidate();

  string verdict = "PASS";
  if (m_iFailures > 0 || m_iChecks != EXPECTED) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbbsettings passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_SettingsTest.MaybeStart();
 }
}
