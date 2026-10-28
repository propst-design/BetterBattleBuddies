// Night illumination uses real carried ammunition and the engine's firing tree.
class BBB_FlareJob : Managed
{
 SCR_AIGroup m_Group;
 SCR_AIUtilityComponent m_Utility;
 BaseMagazineComponent m_Magazine;
 BaseTarget m_Contact;
 vector m_ContactPosition;
 ref BBB_FlareBehavior m_Action;
 float m_Started;
 bool m_Handheld;
}

class BBB_FlareArea : Managed
{
 vector m_Position;
 float m_Until;
}

class BBB_FlareBehavior : SCR_AIFireIllumFlareBehavior
{
 bool m_bUsingHandheld;
 BaseMagazineComponent m_HandheldMagazine;
 int m_HandheldAmmo;

 void CancelItemUse()
 {
  if (!m_bUsingHandheld) return;
  m_bUsingHandheld = false;
  if (!m_Utility || !m_Utility.GetAIAgent()) return;
  ChimeraCharacter body = ChimeraCharacter.Cast(m_Utility.GetAIAgent().GetControlledEntity());
  if (!body || !body.GetCharacterController()) return;
  CharacterControllerComponent controller = body.GetCharacterController();
  if (body.GetWeaponManager().GetCurrentWeapon() != m_WeaponComponent.m_Value || !controller.IsUsingItem()) return;
  CharacterAnimationComponent animation = controller.GetAnimationComponent();
  if (animation && animation.GetCommandHandler()) animation.GetCommandHandler().CancelItemUse();
 }

 override void OnActionFailed()
 {
  CancelItemUse();
  if (m_Utility && m_Utility.m_AIInfo) super.OnActionFailed();
 }

 override void OnActionCompleted()
 {
  if (m_Utility && m_Utility.m_AIInfo) super.OnActionCompleted();
 }

 override void OnActionDeselected()
 {
  CancelItemUse();
  super.OnActionDeselected();
 }

 override float CustomEvaluate()
 {
  if (!BBB_Flares.Allowed(m_Utility)) return 0;
  return GetPriority();
 }

 override void OnActionSelected()
 {
  if (!BBB_Flares.Allowed(m_Utility)) { Fail(); return; }
  super.OnActionSelected();
 }
}

// Native equipping/aiming surrounds this task in the handheld tree. The task
// waits for the physical item-use animation rather than generating a projectile.
class BBB_AIUseHandheldFlare : SCR_AIActionTask
{
 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  BBB_FlareBehavior action = BBB_FlareBehavior.Cast(GetExecutedAction());
  if (!action || !BBB_Flares.Allowed(action.m_Utility)) return ENodeResult.FAIL;
  ChimeraCharacter body = ChimeraCharacter.Cast(owner.GetControlledEntity());
  if (!body) return ENodeResult.FAIL;
  CharacterControllerComponent controller = body.GetCharacterController();
  if (!controller) return ENodeResult.FAIL;
  if (!action.m_bUsingHandheld)
  {
   if (!controller.IsWeaponRaised() || controller.IsChangingItem() || controller.IsUsingItem()) return ENodeResult.RUNNING;
   BaseWeaponComponent weapon = action.m_WeaponComponent.m_Value;
   if (!weapon || body.GetWeaponManager().GetCurrentWeapon() != weapon) return ENodeResult.FAIL;
   action.m_HandheldMagazine = weapon.GetCurrentMagazine();
   if (!action.m_HandheldMagazine || action.m_HandheldMagazine.GetAmmoCount() <= 0) return ENodeResult.FAIL;
   action.m_HandheldAmmo = action.m_HandheldMagazine.GetAmmoCount();
   SCR_FlareAnimationComponent flare = SCR_FlareAnimationComponent.Cast(weapon.GetOwner().FindComponent(SCR_FlareAnimationComponent));
   if (!flare) return ENodeResult.FAIL;
   if (!flare.BBB_PrepareAIUse()) return ENodeResult.RUNNING;
   action.m_bUsingHandheld = controller.TryUseItem(weapon.GetOwner(), false, false);
   return ENodeResult.RUNNING;
  }
  bool consumed = !action.m_HandheldMagazine || action.m_HandheldMagazine.GetAmmoCount() < action.m_HandheldAmmo;
  if (consumed && !controller.IsUsingItem()) return ENodeResult.SUCCESS;
  if (!consumed && !controller.IsUsingItem()) return ENodeResult.FAIL;
  return ENodeResult.RUNNING;
 }

 protected static override bool VisibleInPalette() { return true; }
}

class BBB_Flares
{
 static int s_iRead;
 static bool s_bOn;
 static int s_iDispatched;
 static int s_iCancelled;
 static ref array<ref BBB_FlareJob> s_Jobs = {};
 protected static ref array<ref BBB_FlareArea> s_Areas = {};
 protected static ref map<SCR_AIGroup, float> s_Cooldown = new map<SCR_AIGroup, float>();
 protected static int s_iCursor;
 protected static float s_fNextCheck;
 static float s_fSquadCooldownMs = 120000;
 static float s_fAreaCooldownMs = 90000;
 static const float AREA_RADIUS_M = 125;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbFlares", value) && value == "0");
  if (BBB_Settings.Get("bbbFlaresSquadCooldown", value)) s_fSquadCooldownMs = Math.Clamp(value.ToFloat(), 30, 600) * 1000;
  if (BBB_Settings.Get("bbbFlaresAreaCooldown", value)) s_fAreaCooldownMs = Math.Clamp(value.ToFloat(), 30, 600) * 1000;
  PrintFormat("[BBB] FLARES enabled=%1 squadCooldown=%2s areaCooldown=%3s", s_bOn, s_fSquadCooldownMs / 1000, s_fAreaCooldownMs / 1000);
 }

 // Replace vanilla's independent flare requests only for BBB's registered groups.
 // The same switch therefore cannot create a second source of flare spam.
 static bool Handles(SCR_AIGroup group)
 {
  if (!s_bOn || !BBB.IsStarted() || !Replication.IsServer()) return false;
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  return groups.Contains(group);
 }

 static bool Allowed(SCR_AIUtilityComponent utility)
 {
  if (!s_bOn || !BBB.IsStarted() || !Replication.IsServer() || !utility || !SCR_AIWorldHandling.IsLowLightEnvironment()) return false;
  AIAgent agent = utility.GetAIAgent();
  if (!agent) return false;
  SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
  ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
  if (!group || !body || !BBB.IsAlive(body) || BBB.HasPlayer(group)) return false;
  SCR_AIGroupUtilityComponent groupUtility = group.GetGroupUtilityComponent();
  if (groupUtility && groupUtility.GetCombatModeExternal() == EAIGroupCombatMode.HOLD_FIRE) return false;
  AIControlComponent control = AIControlComponent.Cast(body.FindComponent(AIControlComponent));
  if (!control || !control.IsAIActivated()) return false;
  if (!BBB.s_Hooks.AllowFlares(group) || BBB.s_Hooks.LeaveAlone(body) || BBB_OrderIntent.Blocks(utility)) return false;
  CharacterControllerComponent controller = body.GetCharacterController();
  if (!controller || controller.IsPlayerControlled() || controller.IsUnconscious() || controller.GetDisableWeaponControls()) return false;
  if (body.IsInVehicle() || controller.IsClimbing() || controller.IsSwimming() || BBB_Drag.Busy(body) || BBB_Claims.Committed(body)) return false;
  return true;
 }

 static bool RecentTarget(BaseTarget target, float maxAgeS = 10)
 {
  if (!target || target.GetTargetCategory() != ETargetCategory.ENEMY) return false;
  IEntity entity = target.GetTargetEntity();
  if (!entity) return false;
  ChimeraCharacter character = ChimeraCharacter.Cast(entity);
  if (character && !BBB.IsAlive(character)) return false;
  float seen = target.GetTimeSinceSeen();
  // Sound-only and stale contacts do not authorize illuminating guessed positions.
  return seen >= 0 && seen <= maxAgeS;
 }

 static bool KnownPosition(SCR_AIGroup group, out vector position)
 {
  BaseTarget contact;
  return KnownContact(group, position, contact);
 }

 static bool KnownContact(SCR_AIGroup group, out vector position, out BaseTarget contact)
 {
  contact = null;
  float bestAge = 11;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent || !BBB.IsAlive(agent.GetControlledEntity())) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (!utility || !utility.m_PerceptionComponent) continue;
   array<BaseTarget> targets = {};
   utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
   foreach (BaseTarget target : targets)
   {
    if (!RecentTarget(target)) continue;
    float age = target.GetTimeSinceSeen();
    if (age >= bestAge) continue;
    vector candidate = target.GetLastSeenPosition();
    float distance = vector.DistanceXZ(agent.GetControlledEntity().GetOrigin(), candidate);
    if (distance < 60 || distance > 180) continue;
    contact = target;
    position = candidate;
    bestAge = age;
   }
  }
  return contact != null;
 }

 static bool AreaReady(vector position, float now)
 {
  foreach (BBB_FlareArea area : s_Areas)
   if (area.m_Until > now && vector.DistanceXZ(position, area.m_Position) < AREA_RADIUS_M) return false;
  return true;
 }

 static bool GroupReady(SCR_AIGroup group, float now)
 {
  if (s_Cooldown.Contains(group) && now < s_Cooldown.Get(group)) return false;
  foreach (BBB_FlareJob job : s_Jobs) if (job.m_Group == group) return false;
  return true;
 }

 static bool FindEquipment(SCR_AIUtilityComponent utility, out BaseWeaponComponent weapon, out BaseMagazineComponent magazine, out int muzzle)
 {
  if (!utility || !utility.m_AIInfo) return false;
  if (SCR_AIActivityIllumFlareFeature.GetAgentIllumWeaponAndMuzzle(utility.m_AIInfo, weapon, magazine, muzzle) && magazine && magazine.GetAmmoCount() > 0) return true;
  // Disposable handheld flares have an internal magazine without a magazine well.
  // Identify the native flare component, never arbitrary pistols or item names.
  AIAgent agent = utility.GetAIAgent();
  if (!agent || !agent.GetControlledEntity()) return false;
  IEntity body = agent.GetControlledEntity();
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(body.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return false;
  array<IEntity> items = {};
  inventory.GetItems(items);
  foreach (IEntity item : items)
  {
   if (!item || !item.FindComponent(SCR_FlareAnimationComponent)) continue;
   weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
   if (!weapon) continue;
   magazine = weapon.GetCurrentMagazine();
   if (!magazine || magazine.GetAmmoCount() <= 0) continue;
   muzzle = 0;
   return true;
  }
  weapon = null;
  magazine = null;
  return false;
 }

 static bool ClearLaunch(ChimeraCharacter body, vector aim)
 {
  TraceParam trace = new TraceParam();
  trace.Start = body.GetOrigin() + Vector(0, 1.5, 0);
  trace.End = aim;
  trace.Exclude = body;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  // Also rejects roofs and overhead branches along the elevated launch direction.
  return GetGame().GetWorld().TraceMove(trace, null) >= 1;
 }

 static bool TryIlluminate(SCR_AIGroup group)
 {
  if (!s_bOn || !Replication.IsServer() || !group || BBB.HasPlayer(group) || !BBB.s_Hooks.AllowFlares(group)) return false;
  SCR_AIGroupInfoComponent groupInfo = SCR_AIGroupInfoComponent.Cast(group.FindComponent(SCR_AIGroupInfoComponent));
  if (groupInfo && !groupInfo.IsIllumFlareAllowed()) return false;
  float now = GetGame().GetWorld().GetWorldTime();
  if (!GroupReady(group, now) || !SCR_AIWorldHandling.IsLowLightEnvironment()) return false;
  vector target;
  BaseTarget contact;
  if (!KnownContact(group, target, contact) || !AreaReady(target, now)) return false;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  // A pre-existing native flare action must finish before BBB queues another.
  foreach (AIAgent existingAgent : agents)
  {
   if (!existingAgent) continue;
   SCR_AIUtilityComponent existingUtility = SCR_AIUtilityComponent.Cast(existingAgent.FindComponent(SCR_AIUtilityComponent));
   if (!existingUtility) continue;
   array<ref AIActionBase> existing = {};
   existingUtility.FindActionsOfInheritedType(SCR_AIFireIllumFlareBehavior, existing);
   foreach (AIActionBase action : existing)
    if (!action.GetRemoveAction() && action.GetActionState() != EAIActionState.COMPLETED && action.GetActionState() != EAIActionState.FAILED) return false;
  }
  for (int pass = 0; pass < 2; pass++)
  {
   foreach (AIAgent agent : agents)
   {
    if (!agent) continue;
    SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
    if (!Allowed(utility)) continue;
    if (!utility.m_AIInfo || utility.m_AIInfo.GetAIState() != EUnitAIState.AVAILABLE) continue;
    ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
    CharacterControllerComponent controller = body.GetCharacterController();
    if (controller.IsReloading() || controller.IsChangingItem() || controller.IsUsingItem()) continue;
    float distance = vector.DistanceXZ(body.GetOrigin(), target);
    if (distance < 60 || distance > 180) continue;
    BaseWeaponComponent weapon;
    BaseMagazineComponent magazine;
    int muzzle;
    if (!FindEquipment(utility, weapon, magazine, muzzle)) continue;
    bool handheld = weapon.GetOwner().FindComponent(SCR_FlareAnimationComponent) != null;
    if ((pass == 0 && handheld) || (pass == 1 && !handheld)) continue;
    // Use the native elevated aim, directed at the actual contact sector.
    vector direction = target - body.GetOrigin();
    direction[1] = 0;
    direction.Normalize();
    vector aim = body.GetOrigin() + direction * Math.Clamp(distance, 150, 170);
    vector origin = body.GetOrigin();
    float elevation = 75;
    // The handheld animation adds elevation to its muzzle: the launcher aim
    // would deploy its light above the native 75 m illumination radius.
    if (handheld) elevation = 35;
    aim[1] = Math.Max(origin[1], target[1]) + elevation;
    if (!ClearLaunch(body, aim)) continue;
    BBB_FlareJob job = new BBB_FlareJob();
    job.m_Group = group;
    job.m_Contact = contact;
    job.m_ContactPosition = target;
    job.m_Utility = utility;
    job.m_Magazine = magazine;
    job.m_Started = now;
    job.m_Handheld = weapon.GetOwner().FindComponent(SCR_FlareAnimationComponent) != null;
    BaseMagazineComponent reloadMagazine = magazine;
    if (job.m_Handheld) reloadMagazine = null;
    job.m_Action = new BBB_FlareBehavior(utility, null, aim, weapon, reloadMagazine, muzzle);
    if (job.m_Handheld) job.m_Action.m_sBehaviorTree = "{BBB0F1A200000001}Prefabs/BBB/FireHandheldFlare.bt";
    utility.AddAction(job.m_Action);
    s_Jobs.Insert(job);
    // Reserve immediately, including failed/interrupted requests: no retry storm.
    s_Cooldown.Set(group, now + s_fSquadCooldownMs);
    BBB_FlareArea area = new BBB_FlareArea();
    area.m_Position = target;
    area.m_Until = now + s_fAreaCooldownMs;
    s_Areas.Insert(area);
    if (groupInfo) groupInfo.OnIllumFlareUsed();
    s_iDispatched++;
    PrintFormat("[BBB] FLARES illumination requested by %1 over known contact %2", body, target);
    return true;
   }
  }
  return false;
 }

 static void Maintain()
 {
  float now = GetGame().GetWorld().GetWorldTime();
  array<SCR_AIGroup> groups = {};
  if (!s_Jobs.IsEmpty()) BBB.s_Hooks.GetGroups(groups);
  for (int i = s_Jobs.Count() - 1; i >= 0; i--)
  {
   BBB_FlareJob job = s_Jobs[i];
   bool ended = !job.m_Action || job.m_Action.GetRemoveAction() || job.m_Action.GetActionState() == EAIActionState.COMPLETED || job.m_Action.GetActionState() == EAIActionState.FAILED;
   bool cancelled = !groups.Contains(job.m_Group) || !Allowed(job.m_Utility) || now - job.m_Started > 15000;
   if (!RecentTarget(job.m_Contact, 20)) cancelled = true;
   else if (vector.DistanceXZ(job.m_Contact.GetLastSeenPosition(), job.m_ContactPosition) > AREA_RADIUS_M) cancelled = true;
   if (!ended && !cancelled) continue;
   if (cancelled && !ended) { job.m_Action.Fail(); s_iCancelled++; }
   s_Jobs.Remove(i);
  }
  for (int a = s_Areas.Count() - 1; a >= 0; a--) if (now >= s_Areas[a].m_Until) s_Areas.Remove(a);
  for (int c = s_Cooldown.Count() - 1; c >= 0; c--)
   if (!s_Cooldown.GetKey(c) || now >= s_Cooldown.GetElement(c)) s_Cooldown.RemoveElement(c);
 }

 static void Tick()
 {
  Read();
  if (!Replication.IsServer()) return;
  Maintain();
  if (!s_bOn || !SCR_AIWorldHandling.IsLowLightEnvironment()) return;
  float now = GetGame().GetWorld().GetWorldTime();
  if (now < s_fNextCheck) return;
  s_fNextCheck = now + 2000;
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  // At most four groups per evaluation, one launch per tick across all groups.
  int count = Math.Min(groups.Count(), 4);
  for (int i = 0; i < count; i++)
  {
   if (s_iCursor >= groups.Count()) s_iCursor = 0;
   if (TryIlluminate(groups[s_iCursor++])) break;
  }
 }

 static void Stop()
 {
  foreach (BBB_FlareJob job : s_Jobs)
   if (job.m_Action && !job.m_Action.GetRemoveAction() && job.m_Action.GetActionState() != EAIActionState.COMPLETED && job.m_Action.GetActionState() != EAIActionState.FAILED) job.m_Action.Fail();
  s_Jobs.Clear();
  s_Areas.Clear();
  s_Cooldown.Clear();
  s_iCursor = 0;
  s_fNextCheck = 0;
 }
}

modded class SCR_AIGroupUtilityComponent
{
 override void EvaluateFlareUsage()
 {
  if (BBB_Flares.Handles(SCR_AIGroup.Cast(GetOwner()))) return;
  super.EvaluateFlareUsage();
 }
}

// The native player entry point initializes this flag before TryUseItem. AI use
// needs the same preparation, including the existing prone animation variant.
modded class SCR_FlareAnimationComponent
{
 bool BBB_PrepareAIUse()
 {
  if (!m_UserController || m_UserController.IsPlayerControlled()) return false;
  m_UserController.SetPartialLower(false);
  m_bStartedProne = m_UserController.GetStance() == ECharacterStance.PRONE;
  return CanContinueTheAnimation();
 }
}
