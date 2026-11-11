// Standalone adapter acceptance; omitted from player builds.
class BBB_VanillaTest
{
 // -bbbVanillaTest 1 (preset "bbbvanilla"; GM_Arland, which spawns no AI by itself): BBB
 // started itself, frames run, a spawned AI group is found and named.
 static void Test()
 {
  Resource resource = Resource.Load("{657590C1EC9E27D3}Prefabs/Groups/OPFOR/Group_USSR_LightFireTeam.et");
  EntitySpawnParams parameters = new EntitySpawnParams();
  parameters.TransformMode = ETransformMode.WORLD;
  vector spawnAt = Vector(2880, 0, 1580);
  if (GetGame().GetGameMode()) spawnAt = GetGame().GetGameMode().GetOrigin();
  if (GetGame().GetWorld().GetSurfaceY(spawnAt[0], spawnAt[2]) < 2.0)
  {
   vector mins, maxs;
   GetGame().GetWorld().GetBoundBox(mins, maxs);
   spawnAt = (mins + maxs) * 0.5;
  }
  parameters.Transform[3] = BBB.Ground(spawnAt);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), parameters);
  if (System.IsCLIParam("bbbRearmTest"))
  {
   GetGame().GetCallqueue().CallLater(FinishRearmOnly, 3000, false, spawned);
   return;
  }
  // Rearm mutates inventory and native orders; keep it off the crew fixture.
  s_RearmGroup = SpawnRearmFixture("{657590C1EC9E27D3}Prefabs/Groups/OPFOR/Group_USSR_LightFireTeam.et", BBB.Ground(parameters.Transform[3] + Vector(60, 0, 0)));
  s_AnchorGroup = SpawnRearmFixture("{657590C1EC9E27D3}Prefabs/Groups/OPFOR/Group_USSR_LightFireTeam.et", BBB.Ground(parameters.Transform[3] + Vector(0, 0, 60)));
  int frames = BBB.s_iFrames;
  GetGame().GetCallqueue().CallLater(TestEnd, 3000, false, frames, spawned);
 }

 // Dedicated feature verdict: the full vanilla preset also covers independent
 // vehicle fixtures, whose asynchronous occupancy checks can fail separately.
 static void FinishRearmOnly(IEntity spawned)
 {
  SCR_AIGroup group = SCR_AIGroup.Cast(spawned);
  // Three conditions, each counted (the rearm itself only runs once BBB started with its own hooks).
  bool started = BBB.IsStarted();
  bool hooks = BBB_VanillaHooks.Cast(BBB.s_Hooks) != null;
  bool rearmed = started && hooks && TestRearm(group);
  int passed;
  if (started) passed++;
  if (hooks) passed++;
  if (rearmed) passed++;
  string verdict = "FAIL";
  if (passed == 3) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbrearm passed=%2 failed=%3 expected=3 started=%4 hooks=%5 rearmed=%6", verdict, passed, 3 - passed, started, hooks, rearmed);
  if (spawned) SCR_EntityHelper.DeleteEntityAndChildren(spawned);
  Print("[ACR] TEST COMPLETE");
 }

 static bool TestSelector(SCR_AIGroup group)
 {
  array<EWeaponFiremodeType> m16 = {EWeaponFiremodeType.Safety, EWeaponFiremodeType.Semiauto, EWeaponFiremodeType.Burst};
  array<EWeaponFiremodeType> ak = {EWeaponFiremodeType.Safety, EWeaponFiremodeType.Auto, EWeaponFiremodeType.Semiauto};
  array<EWeaponFiremodeType> bolt = {EWeaponFiremodeType.Safety, EWeaponFiremodeType.Manual};
  array<EWeaponFiremodeType> semi = {EWeaponFiremodeType.Semiauto};
  array<EWeaponFiremodeType> duplicate = {EWeaponFiremodeType.Auto, EWeaponFiremodeType.Auto};
  bool ok = BBB_FireSelector.BestIndex(m16, EWeaponFiremodeType.Auto) == 2;
  ok = ok && BBB_FireSelector.BestIndex(ak, EWeaponFiremodeType.Burst) == 1;
  ok = ok && BBB_FireSelector.BestIndex(bolt, EWeaponFiremodeType.Semiauto) == 1;
  ok = ok && BBB_FireSelector.BestIndex(semi, EWeaponFiremodeType.Safety) == -1;
  ok = ok && BBB_FireSelector.BestIndex(m16, EWeaponFiremodeType.Safety) == 0;
  ok = ok && BBB_FireSelector.BestIndex(duplicate, EWeaponFiremodeType.Auto) == 1;
  // Also exercise the production guard with actual spawned weapons. Every
  // current fireable mode must be accepted; another available type must not.
  int weaponsChecked;
  array<AIAgent> agents = {};
  if (group) group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter soldier = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!soldier) continue;
   array<IEntity> items = {};
   soldier.GetCharacterController().GetWeaponManagerComponent().GetWeaponsList(items);
   foreach (IEntity item : items)
   {
    BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
    if (!weapon || !weapon.GetCurrentMuzzle()) continue;
    BaseMuzzleComponent muzzle = weapon.GetCurrentMuzzle();
    BaseFireMode current = muzzle.GetCurrentFireMode();
    if (!current || current.GetFiremodeType() == EWeaponFiremodeType.Manual) continue;
    ok = ok && BBB_FireSelector.AlreadySelected(muzzle, current.GetFiremodeType());
    array<BaseFireMode> modes = {};
    muzzle.GetFireModesList(modes);
    foreach (BaseFireMode mode : modes)
    {
     if (mode.GetFiremodeType() == current.GetFiremodeType()) continue;
     ok = ok && !BBB_FireSelector.AlreadySelected(muzzle, mode.GetFiremodeType());
    }
    weaponsChecked++;
   }
  }
  PrintFormat("[BBB] SELECTOR regression ok=%1 weapons=%2", ok, weaponsChecked);
  return ok && weaponsChecked > 0;
 }

 static bool TestThreat(SCR_AIGroup group)
 {
  BBB_Threat.Read();
  bool savedOn = BBB_Threat.s_bOn;
  BBB_Threat.s_bOn = true;

  vector origin = Vector(100, 10, 100);
  vector forward = Vector(0, 0, 1);
  vector frontFlat = Vector(100, 10, 150);
  vector frontHigh = Vector(100, 35, 150);
  vector frontLow = Vector(100, 0, 150);
  vector flank90 = Vector(150, 10, 100);
  vector rear180 = Vector(100, 10, 50);

  float multFlat = BBB_Threat.ElevationMultiplier(origin, frontFlat);
  float multHigh = BBB_Threat.ElevationMultiplier(origin, frontHigh);
  float multLow = BBB_Threat.ElevationMultiplier(origin, frontLow);
  bool elevOK = Math.AbsFloat(multFlat - 1.0) < 0.001 && multHigh > 1.35 && multLow < 0.85;

  float flankFront = BBB_Threat.FlankMultiplier(origin, forward, frontFlat);
  float flankSide = BBB_Threat.FlankMultiplier(origin, forward, flank90);
  float flankRear = BBB_Threat.FlankMultiplier(origin, forward, rear180);
  bool flankOK = Math.AbsFloat(flankFront - 1.0) < 0.001 && flankSide > 1.20 && flankRear > flankSide;

  BBB_Threat.s_bGeometry = false;
  float geomOffElev = BBB_Threat.ElevationMultiplier(origin, frontHigh);
  float geomOffFlank = BBB_Threat.FlankMultiplier(origin, forward, flank90);
  BBB_Threat.s_bGeometry = true;
  bool geomSwitchOK = Math.AbsFloat(geomOffElev - 1.0) < 0.001 && Math.AbsFloat(geomOffFlank - 1.0) < 0.001;

  float supNormal = BBB_Threat.ComputeFlybySuppression(0.0, 1, origin, forward, frontFlat, false);
  float supAmbush = BBB_Threat.ComputeFlybySuppression(0.0, 1, origin, forward, frontFlat, true);
  bool ambushOK = supNormal > 0.09 && supAmbush > supNormal + 0.15;

  float decayTuned = BBB_Threat.ComputeDecayedSuppression(0.80, 2000.0);
  BBB_Threat.s_bOn = false;
  float decayBase = BBB_Threat.ComputeDecayedSuppression(0.80, 2000.0);
  BBB_Threat.s_bOn = true;
  bool decayOK = decayTuned > decayBase && decayBase > 0.60;

  float delay0 = BBB_Threat.ComputeExplosionDelayMs(0.0, 0.0);
  float delay34 = BBB_Threat.ComputeExplosionDelayMs(34.3, 0.0);
  float delay68 = BBB_Threat.ComputeExplosionDelayMs(68.6, 0.0);
  bool acousticMathOK = Math.AbsFloat(delay0 - 160.0) < 0.5 && Math.AbsFloat(delay34 - 260.0) < 0.5 && delay68 > delay34;

  bool liveOK = false;
  bool ownSpotOK = false;
  bool reflexOK = false;
  array<AIAgent> agents = {};
  if (group) group.GetAgents(agents);
  if (!agents.IsEmpty() && agents[0])
  {
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agents[0].FindComponent(SCR_AIUtilityComponent));
   if (utility && utility.m_ThreatSystem)
   {
    float s0, sh0, in0, en0;
    utility.m_ThreatSystem.GetThreatValues(s0, sh0, in0, en0);

    utility.m_ThreatSystem.SetThreatValues(0, 0, 0, 0);
    BBB_Threat.s_bOn = false;
    utility.m_ThreatSystem.ThreatExplosion(8.0);
    float expBase, sh, inVal, en;
    utility.m_ThreatSystem.GetThreatValues(expBase, sh, inVal, en);

    utility.m_ThreatSystem.SetThreatValues(0, 0, 0, 0);
    BBB_Threat.s_bOn = true;
    utility.m_ThreatSystem.ThreatExplosion(8.0);
    float expTuned;
    utility.m_ThreatSystem.GetThreatValues(expTuned, sh, inVal, en);

    utility.m_ThreatSystem.SetThreatValues(0.80, 0, 0, 0);
    utility.m_ThreatSystem.Update(utility, 1000.0);
    float afterUpdate;
    utility.m_ThreatSystem.GetThreatValues(afterUpdate, sh, inVal, en);
    float expectedAfter = BBB_Threat.ComputeDecayedSuppression(0.80, 1000.0);

    utility.m_ThreatSystem.SetThreatValues(0.0, 0, 0, 0);
    utility.m_ThreatSystem.Update(utility, 1.0);
    utility.m_fBBBAmbushShockReady = 0;
    vector myPos = utility.m_OwnerEntity.GetOrigin();
    vector behindXZ = myPos - utility.m_OwnerEntity.GetTransformAxis(2) * 40.0;
    float behindGroundY = GetGame().GetWorld().GetSurfaceY(behindXZ[0], behindXZ[2]);
    vector behindHigh = Vector(behindXZ[0], Math.Max(myPos[1] + 18.0, behindGroundY + 18.0), behindXZ[2]);
    if (behindHigh[1] - behindGroundY > 30.0) behindHigh[1] = behindGroundY + 30.0;
    utility.m_ThreatSystem.ThreatProjectileFlyby(1);
    utility.m_SectorThreatFilter.OnShotsFired(behindHigh, 1, true);
    float flybySup;
    utility.m_ThreatSystem.GetThreatValues(flybySup, sh, inVal, en);

    utility.m_ThreatSystem.SetThreatValues(0.0, 0, 0, 0);
    utility.m_ThreatSystem.Update(utility, 1.0);
    utility.m_fBBBAmbushShockReady = 0;
    vector airborne = Vector(behindXZ[0], behindGroundY + 60.0, behindXZ[2]);
    utility.m_ThreatSystem.ThreatProjectileFlyby(1);
    utility.m_SectorThreatFilter.OnShotsFired(airborne, 1, true);
    float airborneSup;
    utility.m_ThreatSystem.GetThreatValues(airborneSup, sh, inVal, en);

    // Two shots heard at the soldier's own spot: the first puts the sector's estimated position on
    // him, the second divided by zero in vanilla (crash.log 25 Oct). Reaching the line after them
    // means no VM exception (the runner also fails on one).
    utility.m_SectorThreatFilter.OnShotsFired(myPos, 1, false);
    utility.m_SectorThreatFilter.OnShotsFired(myPos, 1, false);
    ownSpotOK = true;
    Print("[BBB] THREAT two shots at the soldier's own spot: no division by zero");

    // 343 m/s acoustic explosion reflex lifecycle test on live agent
    BBB_Threat.Stop();
    IEntity soldier = utility.m_OwnerEntity;
    vector blastPos = BBB.Ground(myPos + soldier.GetTransformAxis(2) * 15.0);
    float nowMs = GetGame().GetWorld().GetWorldTime();
    utility.m_ThreatSystem.ThreatExplosion(15.0);
    utility.m_SectorThreatFilter.OnExplosion(blastPos);
    bool scheduledBeforeWave = BBB_Threat.s_iExplosionReflexScheduled == 1 && !BBB_Threat.InExplosionReflex(soldier);
    BBB_Threat.StepReflexJobs(nowMs + 600.0);
    bool appliedAfterWave = BBB_Threat.InExplosionReflex(soldier) && BBB_Threat.s_iExplosionReflexApplied == 1;
    BBB_Threat.StepReflexJobs(nowMs + 10000.0);
    bool releasedAfterHold = !BBB_Threat.InExplosionReflex(soldier);
    BBB_Threat.Stop();
    reflexOK = acousticMathOK && scheduledBeforeWave && appliedAfterWave && releasedAfterHold;

    utility.m_ThreatSystem.SetThreatValues(s0, sh0, in0, en0);
    utility.m_fBBBAmbushShockReady = 0;
    liveOK = expTuned > expBase * 2.0 && expTuned > 0.33 && Math.AbsFloat(afterUpdate - expectedAfter) < 0.01 && flybySup > 0.35 && Math.AbsFloat(airborneSup - 0.10) < 0.001 && ownSpotOK;
   }
  }

  BBB_Threat.s_bOn = savedOn;
  bool ok = elevOK && flankOK && ambushOK && decayOK && liveOK && reflexOK && geomSwitchOK;
  PrintFormat("[BBB] THREAT regression ok=%1 elev=%2 flank=%3 ambush=%4 decay=%5 live=%6 reflex=%7 geomSwitch=%8", ok, elevOK, flankOK, ambushOK, decayOK, liveOK, reflexOK, geomSwitchOK);
  return ok;
 }

 // Pinned in the open keeps alerted perception; pinned in cover, unpinned or switched off
 // leaves vanilla's value (the live perception component has no getter: BBB records what it set).
 static bool TestExposedFire(SCR_AIGroup group)
 {
  array<AIAgent> agents = {};
  if (group) group.GetAgents(agents);
  if (agents.IsEmpty() || !agents[0]) return false;
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agents[0].FindComponent(SCR_AIUtilityComponent));
  if (!utility || !utility.m_ThreatSystem || !utility.m_CombatComponent || !utility.m_CombatMoveState || !utility.m_PerceptionComponent) return false;
  SCR_AICombatComponent combat = utility.m_CombatComponent;
  bool savedOn = BBB_ExposedFire.s_bOn;
  bool savedCover = utility.m_CombatMoveState.m_bInCover;
  float s0, sh0, in0, en0;
  utility.m_ThreatSystem.GetThreatValues(s0, sh0, in0, en0);
  BBB_ExposedFire.Read();
  BBB_ExposedFire.s_bOn = true;

  utility.m_ThreatSystem.SetThreatValues(0.95, 0, 0, 0);
  utility.m_ThreatSystem.Update(utility, 1.0);
  bool pinned = utility.m_ThreatSystem.GetState() == EAIThreatState.THREATENED;
  utility.m_CombatMoveState.m_bInCover = false;
  combat.UpdatePerceptionFactor(utility.m_PerceptionComponent, utility.m_ThreatSystem);
  float open = combat.m_fBBBExposedPerception;
  bool openOK = pinned && Math.AbsFloat(open - BBB_ExposedFire.ALERTED_PERCEPTION * combat.GetPerceptionFactor()) < 0.001;

  utility.m_CombatMoveState.m_bInCover = true;
  combat.UpdatePerceptionFactor(utility.m_PerceptionComponent, utility.m_ThreatSystem);
  bool coverOK = combat.m_fBBBExposedPerception == 0;

  utility.m_CombatMoveState.m_bInCover = false;
  BBB_ExposedFire.s_bOn = false;
  combat.UpdatePerceptionFactor(utility.m_PerceptionComponent, utility.m_ThreatSystem);
  bool offOK = combat.m_fBBBExposedPerception == 0;
  BBB_ExposedFire.s_bOn = true;

  utility.m_ThreatSystem.SetThreatValues(0.0, 0, 0, 0);
  utility.m_ThreatSystem.Update(utility, 1.0);
  combat.UpdatePerceptionFactor(utility.m_PerceptionComponent, utility.m_ThreatSystem);
  bool calmOK = utility.m_ThreatSystem.GetState() != EAIThreatState.THREATENED && combat.m_fBBBExposedPerception == 0;

  utility.m_CombatMoveState.m_bInCover = savedCover;
  utility.m_ThreatSystem.SetThreatValues(s0, sh0, in0, en0);
  utility.m_ThreatSystem.Update(utility, 1.0);
  BBB_ExposedFire.s_bOn = savedOn;
  combat.UpdatePerceptionFactor(utility.m_PerceptionComponent, utility.m_ThreatSystem);
  bool ok = openOK && coverOK && offOK && calmOK;
  PrintFormat("[BBB] EXPOSEDFIRE regression ok=%1 pinnedOpen=%2 (perception %3) inCoverVanilla=%4 offVanilla=%5 calmVanilla=%6", ok, openOK, open, coverOK, offOK, calmOK);
  return ok;
 }

 // BBB #27: cover counts only when it is between the man and his known threat. A sandbag wall
 // is spawned beside a real soldier: with the threat beyond the wall he is covered, with the
 // threat behind him (he stands on the enemy's side of the wall) he is not, leaves that cover
 // once and then has it released. Geometry checks the far-back control (cover must be close).
 static const ResourceName SANDBAG_WALL = "{BE16EE8FAA315FE2}Prefabs/Props/Military/Sandbags/Sandbag_01_long_high_burlap.et";
 static IEntity SpawnWall(vector at)
 {
  Resource resource = Resource.Load(SANDBAG_WALL);
  if (!resource.IsValid()) return null;
  EntitySpawnParams parameters = new EntitySpawnParams();
  parameters.TransformMode = ETransformMode.WORLD;
  parameters.Transform[3] = at;
  return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), parameters);
 }

 static bool TestTrueCover(SCR_AIGroup group)
 {
  array<AIAgent> agents = {};
  if (group) group.GetAgents(agents);
  if (agents.IsEmpty()) return false;
  AIAgent agent = agents[agents.Count() - 1];
  if (!agent) return false;
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (!utility || !utility.m_CombatMoveState || !utility.m_OwnerEntity) return false;
  SCR_AICombatMoveState state = utility.m_CombatMoveState;
  vector man = utility.m_OwnerEntity.GetOrigin();

  // Find the wall's long axis on a probe spawn, then place it with its long face 0.6 m in front of him.
  IEntity probe = SpawnWall(BBB.Ground(man + Vector(0, 0, 40)));
  if (!probe) { Print("[BBB] TRUECOVER regression ok=false (no sandbag prefab)"); return false; }
  vector mins, maxs;
  probe.GetBounds(mins, maxs);
  SCR_EntityHelper.DeleteEntityAndChildren(probe);
  float lengthX = maxs[0] - mins[0];
  float lengthZ = maxs[2] - mins[2];
  float height = maxs[1] - mins[1];
  // Thin across, long along: the man stands off the wall's long face.
  bool longX = lengthX >= lengthZ;
  float half = lengthZ * 0.5;
  vector across = Vector(0, 0, 1);
  if (!longX) { half = lengthX * 0.5; across = Vector(1, 0, 0); }
  vector centreLocal = (mins + maxs) * 0.5;
  centreLocal[1] = 0;
  vector wallAt = BBB.Ground(man + across * (half + 0.6) - centreLocal);
  IEntity wall = SpawnWall(wallAt);
  if (!wall) return false;

  vector beyond = man + across * 60;   // threat on the far side of the wall: he is behind it
  vector behind = man - across * 60;   // threat behind him: he stands on the enemy's side
  bool savedOn = BBB_Cover.s_bOn;
  bool savedCover = state.m_bInCover;
  BBB_Cover.Read();
  BBB_Cover.s_bOn = true;
  bool indoors = BBB_Buildings.BuildingAt(utility) != null;

  bool geometryOK = BBB_Cover.Shields(man, ECharacterStance.CROUCH, beyond) && BBB_Cover.Shields(man, ECharacterStance.PRONE, beyond)
   && !BBB_Cover.Shields(man, ECharacterStance.CROUCH, behind) && !BBB_Cover.Shields(man - across * 10, ECharacterStance.CROUCH, beyond);

  state.m_bInCover = true;
  state.m_fBBBCoverUntil = 0;
  state.m_fBBBCoverLeftAt = 0;
  bool behindOK = BBB_Cover.Faces(utility, beyond);
  bool frontOK = !BBB_Cover.Faces(utility, behind);
  bool keepOK = !BBB_Cover.Leave(utility, state, beyond) && state.m_bInCover;
  int left = BBB_Cover.s_iLeft;
  int released = BBB_Cover.s_iReleased;
  bool leaveOK = BBB_Cover.Leave(utility, state, behind) && BBB_Cover.s_iLeft == left + 1 && state.m_bInCover;
  bool releaseOK = !BBB_Cover.Leave(utility, state, behind) && BBB_Cover.s_iReleased == released + 1 && !state.m_bInCover;

  state.m_bInCover = true;
  state.m_fBBBCoverUntil = 0;
  BBB_Cover.s_bOn = false;
  bool offOK = BBB_Cover.Faces(utility, behind);
  BBB_Cover.s_bOn = savedOn;

  state.m_bInCover = savedCover;
  state.m_fBBBCoverUntil = 0;
  state.m_fBBBCoverLeftAt = 0;
  state.m_bBBBCoverFaces = true;
  SCR_EntityHelper.DeleteEntityAndChildren(wall);
  bool ok = !indoors && height > BBB_Cover.TORSO && geometryOK && behindOK && frontOK && keepOK && leaveOK && releaseOK && offOK;
  PrintFormat("[BBB] TRUECOVER regression ok=%1 wallHeight=%2 indoors=%3 geometry=%4 behindWall=%5 enemySideExposed=%6 keepsRealCover=%7 leaves=%8 thenReleased=%9", ok, height, indoors, geometryOK, behindOK, frontOK, keepOK, leaveOK, releaseOK);
  PrintFormat("[BBB] TRUECOVER off=%1 (off: vanilla flag trusted)", offOK);
  return ok;
 }

 static bool TestAntiArmor(SCR_AIGroup group, IEntity vehicle)
 {
  if (!group || !vehicle) return false;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  if (agents.IsEmpty()) return false;

  ChimeraCharacter soldier0 = ChimeraCharacter.Cast(agents[0].GetControlledEntity());
  if (!soldier0) return false;

  // 1. HasOperationalAT check (rifleman should not have AT)
  bool riflemanHasAT = BBB_Armour.HasOperationalAT(soldier0);

  // 2. The armed M1025 (M2HB turret, perceived unarmoured) is not armour: men shoot its
  // gunner instead of going low, and their rounds aren't held as useless against it.
  bool isThreat = BBB_Armour.IsArmoredThreat(vehicle);
  bool notArmour = !isThreat && !BBB_Armour.Armoured(vehicle);

  // 3. Scatter execution (on by default): call CheckGroupAntiArmor with forced threat
  bool savedScatter = BBB_Armour.s_bScatter;
  bool scatterDefaultOn = BBB_Armour.ScatterOn();
  BBB_Armour.s_bScatter = true;
  BBB_Armour.Stop();
  float nowMs = GetGame().GetWorld().GetWorldTime();
  BBB_Armour.CheckGroupAntiArmor(group, vehicle);
  bool scattered = BBB_Armour.s_iScattered > 0 && BBB_Armour.InAntiArmorJob(soldier0);

  // Check stance setting application
  BBB_AntiArmorJob job = BBB_Armour.JobFor(soldier0);
  bool stanceApplied = job != null && job.m_StanceSetting != null && job.m_eTargetStance != ECharacterStance.STAND;

  // 4. Timeout recovery
  BBB_Armour.StepAntiArmor(nowMs + 30000.0);
  bool recovered = !BBB_Armour.InAntiArmorJob(soldier0) && (job == null || job.m_StanceSetting == null);
  BBB_Armour.Stop();
  BBB_Armour.s_bScatter = savedScatter;
  foreach (AIAgent a : agents)
  {
   ChimeraCharacter c = ChimeraCharacter.Cast(a.GetControlledEntity());
   if (c)
   {
    SCR_CharacterControllerComponent ctl = SCR_CharacterControllerComponent.Cast(c.GetCharacterController());
    if (ctl) SCR_AIStanceHandling.SetStance(ctl, ECharacterStance.STAND);
   }
  }

  bool ok = !riflemanHasAT && notArmour && scatterDefaultOn && scattered && stanceApplied && recovered;
  PrintFormat("[BBB] ANTIARMOR regression ok=%1 noAT=%2 humveeNotArmour=%3 defaultOn=%4 lowStance=%5 stance=%6 recovered=%7", ok, !riflemanHasAT, notArmour, scatterDefaultOn, scattered, stanceApplied, recovered);
  return ok;
 }

 static IEntity SpawnRearmFixture(ResourceName prefab, vector position)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource.IsValid()) return null;
  EntitySpawnParams spawnParams = new EntitySpawnParams();
  spawnParams.TransformMode = ETransformMode.WORLD;
  spawnParams.Transform[3] = position;
  return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawnParams);
 }

 static bool RearmSafety(ChimeraCharacter soldier, IEntity corpse, SCR_AIGroup group, BaseMuzzleComponent muzzle)
 {
  vector original = corpse.GetOrigin();
  int before = BBB_Rearm.s_iMagazinesLooted;
  corpse.SetOrigin(soldier.GetOrigin() + Vector(0, 3, 0));
  bool height = !BBB_Rearm.CanLoot(soldier, corpse) && !BBB_Rearm.LootFromCorpse(soldier, corpse, true, muzzle, false, group);
  corpse.SetOrigin(soldier.GetOrigin() + Vector(5, 0, 0));
  bool distance = !BBB_Rearm.LootFromCorpse(soldier, corpse, true, muzzle, false, group);
  corpse.SetOrigin(original);
  IEntity wall = SpawnRearmFixture("{5EF52FAE299A831A}Prefabs/Props/Military/Sandbags/Sandbag_01_short_base.et", (soldier.GetOrigin() + original) * 0.5);
  bool obstruction = wall && !BBB_Rearm.CanLoot(soldier, corpse) && !BBB_Rearm.LootFromCorpse(soldier, corpse, true, muzzle, false, group);
  if (wall) SCR_EntityHelper.DeleteEntityAndChildren(wall);
  bool unchanged = BBB_Rearm.s_iMagazinesLooted == before;
  float nowMs = GetGame().GetWorld().GetWorldTime();
  BBB_Rearm.RecordFailure(soldier, corpse, nowMs);
  bool retry = !BBB_Rearm.BodyBlocked(soldier, corpse);
  BBB_Rearm.RecordFailure(soldier, corpse, nowMs);
  retry = retry && BBB_Rearm.BodyBlocked(soldier, corpse);
  BBB_Rearm.PruneFailures(nowMs + BBB_Rearm.FAILURE_BACKOFF_MS + 1);
  retry = retry && !BBB_Rearm.BodyBlocked(soldier, corpse);
  AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
  SCR_AIUtilityComponent utility = null;
  if (control && control.GetAIAgent()) utility = SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
  bool orders = false;
  bool timeout = false;
  bool roster = false;
  if (utility)
  {
   BBB_OrderIntentTestHold hold = new BBB_OrderIntentTestHold(utility, null);
   utility.AddAction(hold);
   SCR_AIMoveIndividuallyBehavior trip = new SCR_AIMoveIndividuallyBehavior(utility, null, original, priority: 50);
   BBB_Rearm.SetRearmActionForTest(soldier, trip, corpse);
   BBB_Rearm.CheckRearmFor(group);
   orders = !BBB_Rearm.InRearmAction(soldier) && trip.GetActionState() == EAIActionState.FAILED && !BBB_Rearm.LootFromCorpse(soldier, corpse, true, muzzle, false, group);
   hold.Fail();
   corpse.SetOrigin(soldier.GetOrigin() + Vector(10, 0, 0));
   SCR_AIMoveIndividuallyBehavior stalled = new SCR_AIMoveIndividuallyBehavior(utility, null, corpse.GetOrigin(), priority: 50);
   BBB_Rearm.SetRearmActionForTest(soldier, stalled, corpse);
   timeout = !BBB_Rearm.ExpireTrip(soldier, nowMs) && BBB_Rearm.ExpireTrip(soldier, nowMs + BBB_Rearm.TRIP_TIMEOUT_MS + 1) && stalled.GetActionState() == EAIActionState.FAILED && !BBB_Rearm.InRearmAction(soldier);
   BBB_Rearm.PruneFailures(nowMs + BBB_Rearm.TRIP_TIMEOUT_MS + BBB_Rearm.FAILURE_BACKOFF_MS + 2);
   corpse.SetOrigin(original);
   SCR_AIMoveIndividuallyBehavior removed = new SCR_AIMoveIndividuallyBehavior(utility, null, original, priority: 50);
   BBB_Rearm.SetRearmActionForTest(soldier, removed, corpse);
   array<SCR_AIGroup> emptyRoster = {};
   BBB_Rearm.WatchTrips(emptyRoster);
   roster = !BBB_Rearm.InRearmAction(soldier) && removed.GetActionState() == EAIActionState.FAILED;
  }
  PrintFormat("[BBB] REARM safety height=%1 distance=%2 obstruction=%3 unchanged=%4 retries=%5 orders=%6 timeout=%7 roster=%8", height, distance, obstruction, unchanged, retry, orders, timeout, roster);
  return height && distance && obstruction && unchanged && retry && orders && timeout && roster;
 }

 static bool RearmLauncher(ChimeraCharacter soldier, SCR_AIGroup group)
 {
  IEntity donor = SpawnRearmFixture("{1C78331E156A3D65}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_AT.et", soldier.GetOrigin() + Vector(1.5, 0, 0));
  if (!donor) return false;
  SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(donor);
  if (dmg) dmg.Kill(Instigator.CreateInstigator(null));
  IEntity launcher;
  array<IEntity> rockets = {};
  bool found = BBB_Rearm.CorpseHasAT(donor, launcher, rockets);
  bool compatible = found && !rockets.IsEmpty();
  BaseWeaponComponent weapon = null;
  if (launcher) weapon = BaseWeaponComponent.Cast(launcher.FindComponent(BaseWeaponComponent));
  array<BaseMuzzleComponent> muzzles = {};
  if (weapon) weapon.GetMuzzlesList(muzzles);
  foreach (IEntity rocket : rockets)
  {
   bool matches = false;
   foreach (BaseMuzzleComponent muzzle : muzzles)
   {
    array<IEntity> matching = {};
    BBB_Rearm.CorpseHasMatchingMag(donor, muzzle, matching);
    if (matching.Contains(rocket)) matches = true;
   }
   compatible = compatible && matches;
  }
  int before = BBB_Rearm.s_iLaunchersLooted;
  bool looted = BBB_Rearm.LootFromCorpse(soldier, donor, false, null, true, group);
  bool operational = looted && BBB_Rearm.s_iLaunchersLooted == before + 1 && BBB_Armour.HasOperationalAT(soldier);
  // Restore the original kit so the anti-armor fixture still starts without AT.
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  array<IEntity> acquiredItems = {};
  inv.GetItems(acquiredItems);
  if (launcher && acquiredItems.Contains(launcher))
  {
   inv.TryRemoveItemFromInventory(launcher);
   SCR_EntityHelper.DeleteEntityAndChildren(launcher);
  }
  foreach (IEntity acquiredRocket : rockets)
  {
   if (!acquiredRocket || !acquiredItems.Contains(acquiredRocket)) continue;
   inv.TryRemoveItemFromInventory(acquiredRocket);
   SCR_EntityHelper.DeleteEntityAndChildren(acquiredRocket);
  }
  SCR_EntityHelper.DeleteEntityAndChildren(donor);
  PrintFormat("[BBB] REARM launcher found=%1 compatible=%2 operational=%3", found, compatible, operational);
  return found && compatible && operational;
 }

 static bool RearmTransferFailure(ChimeraCharacter soldier, BaseMuzzleComponent muzzle)
 {
  IEntity donor = SpawnRearmFixture("{DCB41B3746FDD1BE}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Rifleman.et", soldier.GetOrigin() + Vector(1.5, 0, 0));
  if (!donor) return false;
  array<IEntity> mags = {};
  BBB_Rearm.CorpseHasMatchingMag(donor, muzzle, mags);
  if (mags.IsEmpty()) { SCR_EntityHelper.DeleteEntityAndChildren(donor); return false; }
  IEntity item = mags[0];
  InventoryItemComponent itemComp = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
  BaseInventoryStorageComponent source = itemComp.GetParentSlot().GetStorage();
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  array<IEntity> fillers = {};
  bool full = false;
  for (int i = 0; i < 128; i++)
  {
   IEntity filler = SpawnRearmFixture(item.GetPrefabData().GetPrefabName(), soldier.GetOrigin());
   if (!filler) break;
   if (!inv.TryInsertItem(filler))
   {
    full = true;
    SCR_EntityHelper.DeleteEntityAndChildren(filler);
    break;
   }
   fillers.Insert(filler);
  }
  bool failed = full && !BBB_Rearm.TransferItem(inv, item, donor);
  bool restored = itemComp.GetParentSlot() && itemComp.GetParentSlot().GetStorage() == source;
  foreach (IEntity inserted : fillers)
  {
   inv.TryRemoveItemFromInventory(inserted);
   SCR_EntityHelper.DeleteEntityAndChildren(inserted);
  }
  SCR_EntityHelper.DeleteEntityAndChildren(donor);
  PrintFormat("[BBB] REARM insertion full=%1 refused=%2 restored=%3", full, failed, restored);
  return full && failed && restored;
 }

 static bool TestRearm(SCR_AIGroup group)
 {
  if (!group) return false;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  if (agents.IsEmpty()) return false;
  ChimeraCharacter testMan = null;
  if (agents.Count() > 3)
   testMan = ChimeraCharacter.Cast(agents[3].GetControlledEntity());
  if (!testMan)
   testMan = ChimeraCharacter.Cast(agents[0].GetControlledEntity());
  if (!testMan) return false;

  BaseMuzzleComponent muzzle;
  int magCount;
  BBB_Rearm.NeedsAmmo(testMan, muzzle, magCount);

  // Drop carried magazines so the soldier actually needs ammo
  SCR_InventoryStorageManagerComponent sInv = SCR_InventoryStorageManagerComponent.Cast(testMan.FindComponent(SCR_InventoryStorageManagerComponent));
  if (sInv && muzzle && muzzle.GetMagazineWell())
  {
   array<IEntity> carried = {};
   sInv.GetItems(carried);
   string targetWell = muzzle.GetMagazineWell().Type().ToString();
   int removed = 0;
   foreach (IEntity item : carried)
   {
    if (!item) continue;
    MagazineComponent mag = MagazineComponent.Cast(item.FindComponent(MagazineComponent));
    if (mag && mag.GetMagazineWell() && mag.GetMagazineWell().Type().ToString() == targetWell)
    {
     sInv.TryRemoveItemFromInventory(item);
     SCR_EntityHelper.DeleteEntityAndChildren(item);
     removed++;
     if (removed >= 5) break;
    }
   }
  }

  bool needed = BBB_Rearm.NeedsAmmo(testMan, muzzle, magCount);

  Resource res = Resource.Load("{DCB41B3746FDD1BE}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Rifleman.et");
  IEntity corpse = null;
  if (res.IsValid())
  {
   EntitySpawnParams params = new EntitySpawnParams();
   params.TransformMode = ETransformMode.WORLD;
   params.Transform[3] = BBB.Ground(testMan.GetOrigin() + Vector(1.5, 0, 0));
   corpse = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
  }

  bool matchingMag = false;
  bool looted = false;
  bool safety = false;
  if (corpse)
  {
   SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(corpse);
   if (dmg) dmg.Kill(Instigator.CreateInstigator(null));

   array<IEntity> mags = {};
   matchingMag = BBB_Rearm.CorpseHasMatchingMag(corpse, muzzle, mags);

   safety = RearmSafety(testMan, corpse, group, muzzle);
   int beforeLooted = BBB_Rearm.s_iMagazinesLooted;
   BBB_Rearm.LootFromCorpse(testMan, corpse, true, muzzle, false, group);
   looted = BBB_Rearm.s_iMagazinesLooted > beforeLooted;
   SCR_EntityHelper.DeleteEntityAndChildren(corpse);
  }

  bool needsATBefore = BBB_Rearm.NeedsAntiArmor(testMan, group);
  Resource vRes = Resource.Load("{3EA6F47D95867114}Prefabs/Vehicles/Wheeled/M998/M1025_armed_M2HB.et");
  IEntity testVeh = null;
  bool needsATWithVeh = false;
  if (vRes.IsValid())
  {
   EntitySpawnParams vParams = new EntitySpawnParams();
   vParams.TransformMode = ETransformMode.WORLD;
   vParams.Transform[3] = BBB.Ground(testMan.GetOrigin() + Vector(25, 0, 25));
   testVeh = GetGame().SpawnEntityPrefab(vRes, GetGame().GetWorld(), vParams);
  }
  SCR_AIGroupUtilityComponent gUtil = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
  if (gUtil && gUtil.m_Perception && testVeh)
  {
   gUtil.m_Perception.m_aTargetEntities.Insert(testVeh);
   needsATWithVeh = BBB_Rearm.NeedsAntiArmor(testMan, group);
   gUtil.m_Perception.m_aTargetEntities.RemoveItem(testVeh);
   SCR_EntityHelper.DeleteEntityAndChildren(testVeh);
  }
  bool atTriggerOK = !needsATBefore && needsATWithVeh;

  AIControlComponent testControl = AIControlComponent.Cast(testMan.FindComponent(AIControlComponent));
  SCR_AIUtilityComponent testUtil = null;
  if (testControl && testControl.GetAIAgent()) testUtil = SCR_AIUtilityComponent.Cast(testControl.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
  bool cancelOK = false;
  if (testUtil)
  {
   SCR_AIMoveIndividuallyBehavior dummyAct = new SCR_AIMoveIndividuallyBehavior(testUtil, null, testMan.GetOrigin(), priority: 50);
   BBB_Rearm.SetRearmActionForTest(testMan, dummyAct, testMan);
   BBB_Rearm.CancelRearmAction(testMan);
   cancelOK = !BBB_Rearm.InRearmAction(testMan) && dummyAct.GetActionState() == EAIActionState.FAILED;
  }

  bool launcherOK = RearmLauncher(testMan, group);
  bool insertionOK = RearmTransferFailure(testMan, muzzle);
  bool ok = insertionOK && muzzle != null && needed && matchingMag && looted && atTriggerOK && cancelOK && safety && launcherOK;
  PrintFormat("[BBB] REARM regression ok=%1 needed=%2 matchingMag=%3 looted=%4 atTrigger=%5 cancel=%6", ok, needed, matchingMag, looted, atTriggerOK, cancelOK);
  return ok;
 }

 static bool s_bSelectorOK;
 static bool s_bThreatOK;
 static bool s_bAntiArmorOK;
 static bool s_bExposedFireOK;
 static bool s_bTrueCoverOK;
 static bool s_bRearmOK;
 static int s_iFramesCounted;
 static IEntity s_RearmGroup;
 static IEntity s_SpawnedGroup;
 static IEntity s_SpawnedTruck;
 static IEntity s_AnchorGroup;
 static IEntity s_DownedLeader;
 static int s_iAnchor; // 0 pending, 1 pass, -1 fail (anchor and fire-drop checks)
 static ref BBB_FireTeamJob s_FireJob;
 static IEntity s_FireVictim;
 static bool s_bFireDrop1;
 static BaseCompartmentSlot s_TurretSlot;
 static BaseCompartmentSlot s_CargoSlot1;
 static ChimeraCharacter s_Man0;
 static ChimeraCharacter s_Man1;
 static ChimeraCharacter s_Man2;
 static int s_iBeforeReplacements;
 static bool s_bSetupOK;
 static bool s_bAmmoOK;
 static bool s_bPromote1OK;
 static IEntity s_FirstObservedGunner;
 static float s_fSeatDeadline;
 static int s_iSecondPhaseRequests;
 static bool s_bDownObserved;

 // An unconscious leader must not stay the squad's anchor (squads read as strung out
 // around a body that can't walk). Knock the leader out; checked at the end, >= 1 s later.
 static void KnockOutLeader(SCR_AIGroup group)
 {
  s_DownedLeader = null;
  s_iAnchor = -1;
  if (!group) return;
  ChimeraCharacter leader = ChimeraCharacter.Cast(group.GetLeaderEntity());
  if (!leader) return;
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(leader.GetDamageManager());
  if (!damage) return;
  damage.ForceUnconsciousness(0);
  s_DownedLeader = leader;
  s_iAnchor = 0;
  // Unconsciousness wears off within seconds without injuries: check soon after.
  GetGame().GetCallqueue().CallLater(CheckAnchorLater, 700, false);
 }

 static void CheckAnchorLater()
 {
  s_iAnchor = -1;
  if (!CheckAnchor()) return;
  // Same squad, leader still down: a fire-and-maneuver job loses him and fights on.
  s_FireJob = BBB_FireManeuver.TestJob(SCR_AIGroup.Cast(s_AnchorGroup));
  if (!s_FireJob || s_FireJob.m_Members.Count() < 4) { PrintFormat("[BBB] FIREDROP fixture failed"); return; }
  SCR_AIUtilityComponent downed = null;
  foreach (SCR_AIUtilityComponent m : s_FireJob.m_Members)
   if (m.m_OwnerEntity == s_DownedLeader) downed = m;
  int before = s_FireJob.m_Members.Count();
  bool dropped = BBB_FireManeuver.TestDrop(s_FireJob);
  bool holds = BBB_FireManeuver.TestTeamsHold(s_FireJob);
  bool released = downed && !downed.m_BBBFireJob && !s_FireJob.m_Members.Contains(downed);
  bool othersKept = true;
  foreach (SCR_AIUtilityComponent kept : s_FireJob.m_Members)
   if (kept.m_BBBFireJob != s_FireJob) othersKept = false;
  s_bFireDrop1 = dropped && holds && released && othersKept && s_FireJob.m_Members.Count() == before - 1;
  PrintFormat("[BBB] FIREDROP knocked-out ok=%1 dropped=%2 holds=%3 released=%4 othersKept=%5 members=%6->%7", s_bFireDrop1, dropped, holds, released, othersKept, before, s_FireJob.m_Members.Count());
  if (!s_bFireDrop1) return;
  // A second casualty leaves two men: no longer a manoeuvre, the job must end.
  s_FireVictim = s_FireJob.m_Members[s_FireJob.m_Members.Count() - 1].m_OwnerEntity;
  SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(s_FireVictim);
  if (dmg) dmg.Kill(Instigator.CreateInstigator(null));
  s_iAnchor = 0;
  GetGame().GetCallqueue().CallLater(CheckFireDropKilled, 500, false);
 }

 static void CheckFireDropKilled()
 {
  s_iAnchor = -1;
  int before = s_FireJob.m_Members.Count();
  // The dead man's agent (and utility) may already be gone; his body stays.
  bool dead = s_FireVictim && !BBB.IsAlive(s_FireVictim);
  bool dropped = BBB_FireManeuver.TestDrop(s_FireJob);
  bool ends = !BBB_FireManeuver.TestTeamsHold(s_FireJob);
  bool ok = dead && dropped && ends && s_FireJob.m_Members.Count() == before - 1;
  PrintFormat("[BBB] FIREDROP killed ok=%1 dead=%2 dropped=%3 tooFewEnds=%4 members=%5->%6", ok, dead, dropped, ends, before, s_FireJob.m_Members.Count());
  foreach (SCR_AIUtilityComponent m : s_FireJob.m_Members) if (m) m.m_BBBFireJob = null;
  if (ok) s_iAnchor = 1;
 }

 static bool CheckAnchor()
 {
  SCR_AIGroup group = SCR_AIGroup.Cast(s_AnchorGroup);
  ChimeraCharacter leader = ChimeraCharacter.Cast(s_DownedLeader);
  bool down = leader && BBB.IsAlive(leader) && leader.GetCharacterController().IsUnconscious();
  if (leader) PrintFormat("[BBB] ANCHOR downed leader alive=%1 unconscious=%2", BBB.IsAlive(leader), leader.GetCharacterController().IsUnconscious());
  // Vanilla usually hands leadership on; a host (or a moment before it does) can still
  // name the downed man. Put him back as leader so the fallback itself is exercised.
  bool handedOn = group && group.GetLeaderEntity() != leader;
  if (down && group)
  {
   AIControlComponent control = AIControlComponent.Cast(leader.FindComponent(AIControlComponent));
   if (control && control.GetAIAgent()) group.SetNewLeader(control.GetAIAgent());
  }
  IEntity anchor = null;
  if (group) anchor = BBB.AnchorOf(group);
  bool member = false;
  if (group && anchor)
  {
   array<AIAgent> agents = {};
   group.GetAgents(agents);
   foreach (AIAgent agent : agents)
    if (agent && agent.GetControlledEntity() == anchor) member = true;
  }
  bool nativeDowned = group && group.GetLeaderEntity() == leader;
  bool ok = down && nativeDowned && anchor && anchor != leader && member && BBB.IsConscious(anchor) && !BBB.IsConscious(leader);
  PrintFormat("[BBB] ANCHOR regression ok=%1 leaderDown=%2 vanillaHandedOn=%3 nativeLeaderIsDowned=%4 anchorIsDowned=%5 anchorConscious=%6 anchorInGroup=%7", ok, down, handedOn, nativeDowned, anchor == leader, BBB.IsConscious(anchor), member);
  return ok;
 }

 // Confirm fresh native seat occupancy by a fit member. Identity and accepted
 // request counters cannot stand in for an actually occupied turret.
 static IEntity ObservedGunner(SCR_AIGroup group)
 {
  if (!group || !s_SpawnedTruck) return null;
  array<AIAgent> members = {};
  group.GetAgents(members);
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(s_SpawnedTruck, turrets, ECompartmentType.TURRET);
  foreach (BaseCompartmentSlot slot : turrets)
  {
   IEntity occupant = slot.GetOccupant();
   if (!BBB_VehicleHelpers.IsAble(occupant) || BBB.PlayerOf(occupant)) continue;
   foreach (AIAgent member : members)
    if (member && member.GetControlledEntity() == occupant) return occupant;
  }
  return null;
 }


 static void TestGunnerStep2(SCR_AIGroup mine)
 {
  // Twenty seconds allows the normal eight-second retry and native transitions.
  s_fSeatDeadline = GetGame().GetWorld().GetWorldTime() + 20000;
  bool prom1 = BBB_VehicleHelpers.TryReplaceGunner(s_SpawnedTruck, mine);
  GetGame().GetCallqueue().CallLater(TestGunnerStep3, 100, false, mine, prom1);
 }

 static void TestGunnerStep3(SCR_AIGroup mine, bool prom1)
 {
  IEntity occupant = ObservedGunner(mine);
  s_bPromote1OK = occupant != null && BBB_VehicleHelpers.s_iGunnerReplacements > s_iBeforeReplacements;
  if (!s_bPromote1OK && GetGame().GetWorld().GetWorldTime() < s_fSeatDeadline)
  {
   GetGame().GetCallqueue().CallLater(TestGunnerStep3, 100, false, mine, prom1);
   return;
  }
  PrintFormat("[BBB] TURRET first requested=%1 occupied=%2 actor=%3 acceptedRequests=%4", prom1, s_bPromote1OK, occupant, BBB_VehicleHelpers.s_iGunnerReplacements - s_iBeforeReplacements);
  if (!s_bPromote1OK) { s_fSeatDeadline = 0; FinishVanillaTest(mine, false, false); return; }
  s_FirstObservedGunner = occupant;
  s_iSecondPhaseRequests = BBB_VehicleHelpers.s_iGunnerReplacements;
  // A retry can choose a different passenger. Kill the observed gunner, rather
  // than an expected actor who might never have taken the seat.
  SCR_DamageManagerComponent dmg1 = SCR_DamageManagerComponent.GetDamageManager(occupant);
  if (dmg1) dmg1.Kill(Instigator.CreateInstigator(null));
  GetGame().GetCallqueue().CallLater(TestGunnerStep4, 500, false, mine);
 }

 static void TestGunnerStep4(SCR_AIGroup mine)
 {
  // Kill, like boarding, is processed asynchronously. Confirm casualty state
  // here after the native update, rather than immediately after requesting it.
  s_bDownObserved = !BBB_VehicleHelpers.IsAble(s_FirstObservedGunner);
  s_fSeatDeadline = GetGame().GetWorld().GetWorldTime() + 20000;
  bool prom2 = BBB_VehicleHelpers.TryReplaceGunner(s_SpawnedTruck, mine);
  GetGame().GetCallqueue().CallLater(FinishVanillaTest, 100, false, mine, s_bDownObserved, prom2);
 }

 static void FinishVanillaTest(SCR_AIGroup mine, bool recognizedDown, bool prom2)
 {
  if (s_bPromote1OK) recognizedDown = !BBB_VehicleHelpers.IsAble(s_FirstObservedGunner);
  IEntity occupant = ObservedGunner(mine);
  bool downReplaceOK = recognizedDown && occupant != null && occupant != s_FirstObservedGunner && BBB_VehicleHelpers.s_iGunnerReplacements > s_iSecondPhaseRequests;
  if (!downReplaceOK && GetGame().GetWorld().GetWorldTime() < s_fSeatDeadline)
  {
   GetGame().GetCallqueue().CallLater(FinishVanillaTest, 100, false, mine, recognizedDown, prom2);
   return;
  }
  bool turretOK = s_bSetupOK && s_bAmmoOK && s_bPromote1OK && downReplaceOK;
  if (s_iAnchor == 0 && GetGame().GetWorld().GetWorldTime() < s_fSeatDeadline)
  {
   GetGame().GetCallqueue().CallLater(FinishVanillaTest, 100, false, mine, recognizedDown, prom2);
   return;
  }
  bool anchorOK = s_iAnchor == 1;
  PrintFormat("[BBB] TURRET second down=%1 requested=%2 occupied=%3 actor=%4", recognizedDown, prom2, downReplaceOK, occupant);
  PrintFormat("[BBB] TURRET regression ok=%1 setup=%2 ammo=%3 emptyPromote=%4 downReplace=%5", turretOK, s_bSetupOK, s_bAmmoOK, s_bPromote1OK, downReplaceOK);

  bool found = mine != null;
  // The run's twelve conditions, each counted; the names of any that failed are logged.
  array<bool> conditions = {BBB.IsStarted(), BBB_VanillaHooks.Cast(BBB.s_Hooks) != null, found, s_iFramesCounted > 30, s_bSelectorOK, s_bThreatOK, s_bExposedFireOK, s_bTrueCoverOK, turretOK, s_bAntiArmorOK, s_bRearmOK, anchorOK};
  array<string> names = {"started", "hooks", "found", "frames", "selector", "threat", "exposed-fire", "true-cover", "turret", "anti-armour", "rearm", "anchor"};
  int passed;
  string failedNames = "";
  foreach (int c, bool condition : conditions)
  {
   if (condition) passed++;
   else failedNames += " " + names[c];
  }
  string verdict = "FAIL";
  if (passed == conditions.Count()) verdict = "PASS";
  PrintFormat("[BBB] VANILLATEST %1%2", verdict, failedNames);
  PrintFormat("[ACR] VERDICT %1 test=bbbvanilla started=%2 found=%3 frames=%4 passed=%5 failed=%6 expected=%7", verdict, BBB.IsStarted(), found, s_iFramesCounted, passed, conditions.Count() - passed, conditions.Count());
  if (s_SpawnedTruck) SCR_EntityHelper.DeleteEntityAndChildren(s_SpawnedTruck);
  if (s_SpawnedGroup) SCR_EntityHelper.DeleteEntityAndChildren(s_SpawnedGroup);
  if (s_AnchorGroup) SCR_EntityHelper.DeleteEntityAndChildren(s_AnchorGroup);
  Print("[ACR] TEST COMPLETE");
 }

 static void TestEnd(int framesBefore, IEntity spawned)
 {
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  string name = "";
  SCR_AIGroup mine = SCR_AIGroup.Cast(spawned);
  if (mine) name = BBB.s_Hooks.NameOf(mine);
  bool found = mine && groups.Contains(mine);
  s_iFramesCounted = BBB.s_iFrames - framesBefore;
  s_SpawnedGroup = spawned;
  PrintFormat("[BBB] VANILLATEST started=%1 hooks=%2 groups=%3 found=%4 name='%5' frames in 3 s=%6", BBB.IsStarted(), BBB.s_Hooks.Type().ToString(), groups.Count(), found, name, s_iFramesCounted);

  s_bSelectorOK = TestSelector(mine);
  s_bThreatOK = TestThreat(mine);
  s_bExposedFireOK = TestExposedFire(mine);
  s_bTrueCoverOK = TestTrueCover(mine);
  s_bRearmOK = TestRearm(SCR_AIGroup.Cast(s_RearmGroup));
  KnockOutLeader(SCR_AIGroup.Cast(s_AnchorGroup));
  if (s_RearmGroup) SCR_EntityHelper.DeleteEntityAndChildren(s_RearmGroup);

  array<AIAgent> agents = {};
  if (mine) mine.GetAgents(agents);
  if (agents.Count() >= 3)
  {
   s_Man0 = ChimeraCharacter.Cast(agents[0].GetControlledEntity());
   s_Man1 = ChimeraCharacter.Cast(agents[1].GetControlledEntity());
   s_Man2 = ChimeraCharacter.Cast(agents[2].GetControlledEntity());
  }

  Resource res = Resource.Load("{3EA6F47D95867114}Prefabs/Vehicles/Wheeled/M998/M1025_armed_M2HB.et");
  if (res.IsValid() && s_Man0 && s_Man1 && s_Man2)
  {
   EntitySpawnParams params = new EntitySpawnParams();
   params.TransformMode = ETransformMode.WORLD;
   params.Transform[3] = BBB.Ground(s_Man0.GetOrigin() + Vector(8, 0, 8));
   s_SpawnedTruck = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
  }

  SCR_BaseCompartmentManagerComponent comps;
  array<BaseCompartmentSlot> pilots = {};
  array<BaseCompartmentSlot> turrets = {};
  array<BaseCompartmentSlot> cargos = {};
  if (s_SpawnedTruck)
  {
   BBB_VehicleHelpers.GetVehicleCompartments(s_SpawnedTruck, pilots, ECompartmentType.PILOT);
   BBB_VehicleHelpers.GetVehicleCompartments(s_SpawnedTruck, turrets, ECompartmentType.TURRET);
   BBB_VehicleHelpers.GetVehicleCompartments(s_SpawnedTruck, cargos, ECompartmentType.CARGO);
  }

  s_bSetupOK = s_SpawnedTruck != null && !pilots.IsEmpty() && !turrets.IsEmpty() && cargos.Count() >= 2;
  s_bAmmoOK = false;
  s_bPromote1OK = false;
  s_FirstObservedGunner = null;
  s_bDownObserved = false;
  s_fSeatDeadline = 0;
  s_iBeforeReplacements = BBB_VehicleHelpers.s_iGunnerReplacements;

  if (s_bSetupOK)
  {
   s_bAntiArmorOK = TestAntiArmor(mine, s_SpawnedTruck);

   s_TurretSlot = turrets[0];
   s_CargoSlot1 = cargos[1];
   s_bAmmoOK = BBB_VehicleHelpers.HasTurret(s_SpawnedTruck) && !BBB_VehicleHelpers.HasGunner(s_SpawnedTruck) && BBB_VehicleHelpers.TurretHasAmmo(s_TurretSlot);

   s_Man0.GetCompartmentAccessComponent().GetInVehicle(pilots[0].GetOwner(), pilots[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
   s_Man1.GetCompartmentAccessComponent().GetInVehicle(cargos[0].GetOwner(), cargos[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
   s_Man2.GetCompartmentAccessComponent().GetInVehicle(cargos[1].GetOwner(), cargos[1], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);

   GetGame().GetCallqueue().CallLater(TestGunnerStep2, 1000, false, mine);
   return;
  }

  FinishVanillaTest(mine, false, false);
 }

}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  if (Replication.IsServer() && (System.IsCLIParam("bbbVanillaTest") || System.IsCLIParam("acrBbbVanillaTest") || System.IsCLIParam("bbbRearmTest")))
   GetGame().GetCallqueue().CallLater(BBB_VanillaTest.Test, 25000);
 }
}

// Test access to the fire-and-maneuver casualty handling (job is never registered).
modded class BBB_FireManeuver
{
 static BBB_FireTeamJob TestJob(SCR_AIGroup group)
 {
  if (!group) return null;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  BBB_FireTeamJob job = new BBB_FireTeamJob();
  job.m_Group = group;
  foreach (AIAgent agent : agents)
  {
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (utility) job.m_Members.Insert(utility);
  }
  foreach (int index, SCR_AIUtilityComponent member : job.m_Members)
  {
   member.m_BBBFireJob = job;
   member.m_iBBBFireTeam = 1;
   if (index < job.m_Members.Count() / 2) member.m_iBBBFireTeam = 0;
  }
  return job;
 }
 static bool TestDrop(BBB_FireTeamJob job)
 {
  array<AIAgent> roster = {};
  BBB.s_Hooks.GetMembers(job.m_Group, roster);
  return DropLost(job, roster);
 }
 static bool TestTeamsHold(BBB_FireTeamJob job) { return TeamsHold(job); }
}
