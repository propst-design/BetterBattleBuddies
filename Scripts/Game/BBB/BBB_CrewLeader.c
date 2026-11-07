// Crew leader (Jacob, 7 Nov 2026: "if the BMP driver called out his contacts, or the commander spotted
// them himself, and the commander told the gunner where they were and to shoot..."). Vanilla has no
// fire control for a crew: every crewman spots for himself, the gunner's own target picker decides what
// he shoots, and vanilla's vehicle combat activity (SCR_AIVehicleCombatActivity) only dismounts riders
// and replaces drivers. BBB's call-out (BBB_CrewCallout) let a gunner who saw nothing take his crew's
// word; the gunner still fought his own fight once he saw anything at all.
//
// Now one man commands each AI-crewed armed vehicle: the commander (a commander's seat), else the
// driver. Once a second he builds the crew's picture and gives the gunner one target:
//  - the picture: enemies any AI aboard recognised within SEEN_S (crew and riders), the gunner's own,
//    and those of his squadmates on foot within DISMOUNT_M (infantry calling targets to its vehicle).
//    Real sightings only, each at its freshest last seen spot.
//  - the target: sightings grouped GROUP_M round each other and scored by what they are (a man, a man
//    with a loaded launcher, light / medium / heavy vehicle), more for those shooting at the crew, less
//    with range. Armour is left out when the gunner's turret has nothing that hurts it. The leader
//    keeps his target unless another is worth KEEP_RATIO more or his has stood KEEP_S.
//  - the order, by what the gunner is doing (BBB_HotAI's fire-control loop, once a second):
//     sees nothing: turn onto the target (vanilla's commander look priority) and, if still blind after
//       BBB_HotAI.CALLOUT_FIRE_S, a short series of fire into a box round its men;
//     fighting something worth less (SWITCH_RATIO) and sees one of the leader's men: his target is
//       switched to that man (his own perception's target, with the weapon his picker gives for it) and
//       held while it stays the leader's target (HOLD_S, renewed each second);
//     fighting something worth far less (OVERRIDE_RATIO), farther than OVERRIDE_MIN_M, and blind to
//       the leader's target: the commander breaks off his fight and lays him on it with a fire series
//       that keeps going until he sees one of its men (then the switch above);
//     a man shooting at the crew is weighed by the leader like any other (SHOOTING in his score): a
//       lone rifleman shooting at 70 m loses to four men at 230 m, a launcher man shooting beats almost
//       anything. (A hard "shooting at him: his own fight" rule let a hold-fire decoy, which vanilla
//       counted as endangering, keep the gunner off the group the leader gave him: test run 4.)
//  - the leader says it: vanilla's REPORT_TARGET call-out when he gives a new target.
// Gates: hold fire, a player in the crew or the squad, the host's LeaveAlone, BBB_OrderIntent.GunnerBlocks.
//  -bbbCrewLeader 0   off: the crew call-out (BBB_CrewCallout) as before
class BBB_CrewSighting : Managed
{
 IEntity m_Enemy;
 vector m_vPos;
 float m_fAge;
 bool m_bShooting;   // shot at the crew within ENDANGER_S
 EAIUnitType m_eType;
 float m_fValue;     // what he is worth on his own
 float m_fGroup;     // the score of his group (Pick)
}

class BBB_CrewOrder : Managed
{
 IEntity m_Vehicle;
 IEntity m_Leader;      // who commands; null: nobody (no AI commander or driver, or a player aboard)
 float m_fBuilt;        // world time s
 bool m_bArmour;        // the gunner's turret has something that hurts armour
 ref array<ref BBB_CrewSighting> m_aSeen = {};
 // The leader's target: a group of enemies.
 bool m_bHas;
 IEntity m_Key;         // the man whose last seen spot is the aim
 vector m_vAim;
 float m_fSpread;       // farthest of its men from the aim (m)
 int m_iMen;
 int m_iShooting;
 float m_fScore;
 string m_sWhat;        // for the log and Think
 ref array<IEntity> m_aMen = {};
 float m_fSince;        // world time s he gave this target
 float m_fSaidAt;       // world time s of his last call-out

 // The score of the group this enemy is in; one he doesn't know scores as a lone man at pos.
 float ScoreOf(IEntity enemy, vector pos)
 {
  foreach (BBB_CrewSighting seen : m_aSeen)
   if (seen.m_Enemy == enemy) return seen.m_fGroup;
  float score = BBB_CrewLeader.MAN;
  if (m_Vehicle) score -= BBB_CrewLeader.RANGE_PENALTY * vector.DistanceXZ(m_Vehicle.GetOrigin(), pos);
  return score;
 }
}

class BBB_CrewLeader
{
 static bool s_bOn;       // tests set it per arm
 protected static int s_iRead;
 protected static ref map<IEntity, ref BBB_CrewOrder> s_mOrders;
 protected static ref map<IEntity, float> s_mATUntil; // enemy -> world s his launcher check expires
 protected static ref map<IEntity, bool> s_mAT;
 protected static ref array<BaseTarget> s_aTargets;
 protected static ref array<BaseCompartmentSlot> s_aSlots;
 protected static ref array<AIAgent> s_aAgents;
 protected static float s_fPruneAt;
 static int s_iOrders;    // targets given (tests and the log)
 static int s_iSwitches;  // gunners switched onto the leader's target
 static int s_iOverrides; // fights broken off to lay the gunner on it
 static int s_iBlindFires; // fire series on the leader's target by a gunner who saw nothing
 static int s_iSaid;      // call-outs spoken

 static const float SEEN_S = 4;
 static const float GROUP_M = 25;
 static const float MAN = 100;
 static const float AT_MAN = 250;
 static const float SHOOTING = 150;
 static const float VEHICLE_LIGHT = 150;
 static const float VEHICLE_MEDIUM = 400;
 static const float VEHICLE_HEAVY = 600;
 static const float RANGE_PENALTY = 0.1;
 static const float ENDANGER_S = 10;
 static const float DISMOUNT_M = 300;
 static const float KEEP_RATIO = 1.5;
 static const float KEEP_S = 6;
 static const float SWITCH_RATIO = 1.5;
 static const float OVERRIDE_RATIO = 2.5;
 static const float OVERRIDE_MIN_M = 60;
 static const float OVERRIDE_EVERY_S = 15;
 static const float HOLD_S = 3;        // renewed each second while it stays the leader's target
 static const float SAY_EVERY_S = 10;
 static const float AT_CACHE_S = 20;
 static const int MAX_SEEN = 32;

 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_mOrders = new map<IEntity, ref BBB_CrewOrder>();
  s_mATUntil = new map<IEntity, float>();
  s_mAT = new map<IEntity, bool>();
  s_aTargets = {};
  s_aSlots = {};
  s_aAgents = {};
 }

 static bool On()
 {
  if (BBB_Settings.Due(s_iRead))
  {
   string value;
   s_bOn = !(BBB_Settings.Get("bbbCrewLeader", value) && value == "0");
   PrintFormat("[BBB] CREW LEADER enabled=%1", s_bOn);
  }
  return s_bOn;
 }

 static void Stop()
 {
  if (s_mOrders) s_mOrders.Clear();
  if (s_mATUntil) s_mATUntil.Clear();
  if (s_mAT) s_mAT.Clear();
 }

 protected static float Now() { return GetGame().GetWorld().GetWorldTime() / 1000; }

 // This vehicle's order, built at most once a second; null when nobody commands it.
 static BBB_CrewOrder Get(IEntity vehicle, IEntity gunner)
 {
  if (!On() || !vehicle || !s_mOrders) return null;
  float now = Now();
  Prune(now);
  BBB_CrewOrder order = s_mOrders.Get(vehicle);
  if (!order)
  {
   order = new BBB_CrewOrder();
   order.m_Vehicle = vehicle;
   order.m_fBuilt = -1000;
   s_mOrders.Set(vehicle, order);
  }
  if (now - order.m_fBuilt >= 1) Build(order, gunner, now);
  if (!order.m_Leader) return null;
  return order;
 }

 protected static void Prune(float now)
 {
  if (now < s_fPruneAt) return;
  s_fPruneAt = now + 30;
  array<IEntity> gone = {};
  foreach (IEntity vehicle, BBB_CrewOrder order : s_mOrders)
   if (!vehicle || now - order.m_fBuilt > 30) gone.Insert(vehicle);
  foreach (IEntity old : gone) s_mOrders.Remove(old);
  gone.Clear();
  foreach (IEntity enemy, float until : s_mATUntil)
   if (!enemy || now > until) gone.Insert(enemy);
  foreach (IEntity stale : gone)
  {
   s_mATUntil.Remove(stale);
   s_mAT.Remove(stale);
  }
 }

 // ------------------------------------------------------------------ the picture

 protected static void Build(BBB_CrewOrder order, IEntity gunner, float now)
 {
  order.m_fBuilt = now;
  order.m_aSeen.Clear();
  IEntity vehicle = order.m_Vehicle;
  IEntity before = order.m_Leader;
  order.m_Leader = LeaderOf(vehicle);
  if (!order.m_Leader)
  {
   order.m_bHas = false;
   return;
  }
  if (before && before != order.m_Leader)
  {
   // Why the last one stopped leading: down, or out of his seat (vanilla dismounts "cargo" in a fight).
   string why = "out of the vehicle";
   if (!BBB.IsAlive(before)) why = "dead";
   else if (!BBB.IsConscious(before)) why = "unconscious";
   else if (BBB_Transport.VehicleOf(before) == vehicle) why = "moved to the " + SeatName(before) + " seat";
   PrintFormat("[BBB] CREW LEADER %1: the %2 takes command (the last leader is %3)", Name(vehicle), SeatName(order.m_Leader), why);
  }
  order.m_bArmour = HurtsArmour(gunner);

  // Everyone aboard: crew and riders.
  BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
  if (compartments)
  {
   s_aSlots.Clear();
   compartments.GetCompartments(s_aSlots);
   foreach (BaseCompartmentSlot slot : s_aSlots)
   {
    IEntity mate = slot.GetOccupant();
    if (!mate || BBB.PlayerOf(mate) || !BBB.IsConscious(mate)) continue;
    Collect(order, mate, true);
   }
   s_aSlots.Clear();
  }
  // Squadmates on foot near the vehicle.
  SCR_AIGroup group = BBB_HeldGuns.GroupOf(gunner);
  if (group)
  {
   s_aAgents.Clear();
   group.GetAgents(s_aAgents);
   vector at = vehicle.GetOrigin();
   foreach (AIAgent agent : s_aAgents)
   {
    if (!agent) continue;
    IEntity man = agent.GetControlledEntity();
    if (!man || BBB_Transport.VehicleOf(man) || !BBB.IsConscious(man)) continue;
    if (vector.DistanceXZ(at, man.GetOrigin()) > DISMOUNT_M) continue;
    Collect(order, man, false);
   }
   s_aAgents.Clear();
  }
  Pick(order, now);
 }

 // The commander: a commander's seat (a turret-linked or in-turret cargo seat, or a turret seat
 // without a gun), else the driver. Nobody when a player sits in a crew seat: he is in charge.
 protected static IEntity LeaderOf(IEntity vehicle)
 {
  BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
  if (!compartments) return null;
  s_aSlots.Clear();
  compartments.GetCompartments(s_aSlots);
  IEntity commander;
  IEntity driver;
  bool player;
  foreach (BaseCompartmentSlot slot : s_aSlots)
  {
   IEntity man = slot.GetOccupant();
   if (!man) continue;
   bool crew = slot.GetType() == ECompartmentType.PILOT || slot.GetType() == ECompartmentType.TURRET || BBB_Transport.CommanderSeat(slot);
   if (!crew) continue;
   if (BBB.PlayerOf(man))
   {
    player = true;
    break;
   }
   if (!BBB.IsConscious(man)) continue;
   if (slot.GetType() == ECompartmentType.PILOT)
   {
    driver = man;
    continue;
   }
   if (!commander && (BBB_Transport.CommanderSeat(slot) || !TurretGun(slot))) commander = man;
  }
  s_aSlots.Clear();
  if (player) return null;
  if (commander) return commander;
  return driver;
 }

 // A turret seat with a gun mounted (any weapon slot holding a weapon). Not the current weapon: that
 // is empty for a moment while he reloads or changes guns, which made the gunner "commander" (test run).
 protected static bool TurretGun(BaseCompartmentSlot slot)
 {
  TurretCompartmentSlot turret = TurretCompartmentSlot.Cast(slot);
  if (!turret) return false;
  TurretControllerComponent controller = TurretControllerComponent.Cast(turret.GetController());
  if (!controller || !controller.GetWeaponManager()) return false;
  array<WeaponSlotComponent> weapons = {};
  controller.GetWeaponManager().GetWeaponsSlots(weapons);
  foreach (WeaponSlotComponent weapon : weapons)
   if (weapon && weapon.GetWeaponEntity()) return true;
  return false;
 }

 // An autocannon or launcher in the gunner's turret (his picker's own cache of the seat's weapons).
 protected static bool HurtsArmour(IEntity gunner)
 {
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(gunner);
  if (!utility || !utility.m_CombatComponent || !utility.m_CombatComponent.m_WeaponTargetSelector) return false;
  AIWeaponTargetSelector selector = utility.m_CombatComponent.m_WeaponTargetSelector;
  return selector.HasWeaponOfType(EWeaponType.WT_AUTOCANNON, true) || selector.HasWeaponOfType(EWeaponType.WT_ROCKETLAUNCHER, true);
 }

 protected static void Collect(BBB_CrewOrder order, IEntity eyesOf, bool crew)
 {
  PerceptionComponent eyes = PerceptionComponent.Cast(eyesOf.FindComponent(PerceptionComponent));
  if (!eyes) return;
  s_aTargets.Clear();
  eyes.GetTargetsList(s_aTargets, ETargetCategory.ENEMY);
  foreach (BaseTarget target : s_aTargets)
  {
   if (!target || target.IsDisarmed()) continue;
   float age = target.GetTimeSinceSeen();
   if (age > SEEN_S) continue;
   IEntity enemy = target.GetTargetEntity();
   if (!enemy || target.GetUnitType() == EAIUnitType.UnitType_Aircraft || BBB_SuppressionSafety.AircraftSource(enemy)) continue;
   bool shot = crew && target.GetTimeSinceEndangered() < ENDANGER_S;
   BBB_CrewSighting known;
   foreach (BBB_CrewSighting seen : order.m_aSeen)
   {
    if (seen.m_Enemy != enemy) continue;
    known = seen;
    break;
   }
   if (known)
   {
    if (age < known.m_fAge)
    {
     known.m_fAge = age;
     known.m_vPos = target.GetLastSeenPosition();
    }
    if (shot) known.m_bShooting = true;
    continue;
   }
   if (order.m_aSeen.Count() >= MAX_SEEN) continue;
   BBB_CrewSighting sighting = new BBB_CrewSighting();
   sighting.m_Enemy = enemy;
   sighting.m_vPos = target.GetLastSeenPosition();
   sighting.m_fAge = age;
   sighting.m_bShooting = shot;
   sighting.m_eType = target.GetUnitType();
   order.m_aSeen.Insert(sighting);
  }
  s_aTargets.Clear();
 }

 // What one enemy is worth on his own (the squad leader scores with it too).
 static float ThreatValue(EAIUnitType type, IEntity enemy, bool shooting, float now)
 {
  float value = MAN;
  if (type == EAIUnitType.UnitType_VehicleUnarmored) value = VEHICLE_LIGHT;
  else if (type == EAIUnitType.UnitType_VehicleMedium) value = VEHICLE_MEDIUM;
  else if (type == EAIUnitType.UnitType_VehicleHeavy) value = VEHICLE_HEAVY;
  else if (HasAT(enemy, now)) value = AT_MAN;
  if (shooting) value += SHOOTING;
  return value;
 }

 protected static float Value(BBB_CrewSighting seen, float now)
 {
  return ThreatValue(seen.m_eType, seen.m_Enemy, seen.m_bShooting, now);
 }

 protected static bool Armour(EAIUnitType type)
 {
  return type == EAIUnitType.UnitType_VehicleMedium || type == EAIUnitType.UnitType_VehicleHeavy;
 }

 // A man with a loaded launcher (or rockets for it), cached AT_CACHE_S per man.
 static bool HasAT(IEntity enemy, float now)
 {
  if (!enemy || !s_mATUntil) return false;
  float until;
  if (s_mATUntil.Find(enemy, until) && now < until) return s_mAT.Get(enemy);
  ChimeraCharacter character = ChimeraCharacter.Cast(enemy);
  bool at = character && BBB_Armour.HasOperationalAT(character);
  s_mATUntil.Set(enemy, now + AT_CACHE_S);
  s_mAT.Set(enemy, at);
  return at;
 }

 // ------------------------------------------------------------------ the target

 protected static void Pick(BBB_CrewOrder order, float now)
 {
  int count = order.m_aSeen.Count();
  vector at = order.m_Vehicle.GetOrigin();
  foreach (BBB_CrewSighting seen : order.m_aSeen) seen.m_fValue = Value(seen, now);
  int best = -1;
  float bestScore = -float.MAX;
  int keep = -1;
  float keepScore = -float.MAX;
  for (int i = 0; i < count; i++)
  {
   BBB_CrewSighting one = order.m_aSeen[i];
   float score = -RANGE_PENALTY * vector.DistanceXZ(at, one.m_vPos);
   for (int j = 0; j < count; j++)
   {
    BBB_CrewSighting other = order.m_aSeen[j];
    if (vector.DistanceXZ(one.m_vPos, other.m_vPos) <= GROUP_M) score += other.m_fValue;
   }
   one.m_fGroup = score;
   if (Armour(one.m_eType) && !order.m_bArmour) continue; // nothing in his turret hurts it
   if (order.m_bHas && order.m_aMen.Contains(one.m_Enemy) && score > keepScore)
   {
    keep = i;
    keepScore = score;
   }
   if (score > bestScore)
   {
    best = i;
    bestScore = score;
   }
  }
  if (best < 0)
  {
   order.m_bHas = false;
   order.m_aMen.Clear();
   return;
  }
  int chosen = best;
  bool fresh = true;
  if (keep >= 0)
  {
   // He keeps his target unless another is clearly worth more or his has stood long enough.
   bool clearly = bestScore >= KEEP_RATIO * Math.Max(keepScore, 1);
   bool stood = now - order.m_fSince >= KEEP_S && bestScore > keepScore;
   if (best == keep || !(clearly || stood))
   {
    chosen = keep;
    fresh = false;
   }
   else fresh = !InGroup(order.m_aSeen[best], order.m_aSeen[keep]);
  }
  Fill(order, order.m_aSeen[chosen]);
  if (!fresh) return;
  order.m_fSince = now;
  s_iOrders++;
  PrintFormat("[BBB] CREW LEADER %1: the %2 gives the target %3 at %4 m (score %5)", Name(order.m_Vehicle), SeatName(order.m_Leader), order.m_sWhat, Math.Round(vector.DistanceXZ(at, order.m_vAim)), Math.Round(order.m_fScore));
  BBB_Think.Note(order.m_Leader, "CrewLeader", "gives the gunner a target: " + order.m_sWhat + ", " + Math.Round(vector.DistanceXZ(at, order.m_vAim)) + " m");
  Say(order, now);
 }

 protected static bool InGroup(BBB_CrewSighting a, BBB_CrewSighting b)
 {
  return vector.DistanceXZ(a.m_vPos, b.m_vPos) <= GROUP_M;
 }

 protected static void Fill(BBB_CrewOrder order, BBB_CrewSighting key)
 {
  order.m_bHas = true;
  order.m_Key = key.m_Enemy;
  order.m_vAim = key.m_vPos;
  order.m_fScore = key.m_fGroup;
  order.m_aMen.Clear();
  order.m_iMen = 0;
  order.m_iShooting = 0;
  order.m_fSpread = 0;
  int vehicles;
  int launchers;
  float now = Now();
  foreach (BBB_CrewSighting seen : order.m_aSeen)
  {
   float apart = vector.DistanceXZ(key.m_vPos, seen.m_vPos);
   if (apart > GROUP_M) continue;
   order.m_aMen.Insert(seen.m_Enemy);
   order.m_iMen++;
   if (seen.m_bShooting) order.m_iShooting++;
   if (seen.m_eType != EAIUnitType.UnitType_Infantry) vehicles++;
   else if (HasAT(seen.m_Enemy, now)) launchers++;
   if (apart > order.m_fSpread) order.m_fSpread = apart;
  }
  order.m_sWhat = order.m_iMen.ToString() + " enemies";
  if (vehicles > 0) order.m_sWhat = order.m_sWhat + " (" + vehicles.ToString() + " vehicles)";
  if (launchers > 0) order.m_sWhat = order.m_sWhat + " (" + launchers.ToString() + " with launchers)";
  if (order.m_iShooting > 0) order.m_sWhat = order.m_sWhat + ", " + order.m_iShooting.ToString() + " shooting at us";
 }

 // Vanilla's "target" call-out, from the leader, through his comms queue.
 protected static void Say(BBB_CrewOrder order, float now)
 {
  if (now - order.m_fSaidAt < SAY_EVERY_S || !order.m_Key) return;
  SCR_AIUtilityComponent speaker = BBB_VehiclePosture.Utility(order.m_Leader);
  if (!speaker || !speaker.m_CommsHandler || speaker.m_CommsHandler.CanBypass()) return;
  speaker.m_CommsHandler.AddRequest(new SCR_AITalkRequest(ECommunicationType.REPORT_TARGET, order.m_Key, order.m_vAim, 0, true, true, SCR_EAITalkRequestPreset.MEDIUM));
  order.m_fSaidAt = now;
  s_iSaid++;
 }

 protected static string SeatName(IEntity man)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(man);
  if (!character || !character.GetCompartmentAccessComponent()) return "crewman";
  BaseCompartmentSlot slot = character.GetCompartmentAccessComponent().GetCompartment();
  if (!slot) return "crewman";
  if (slot.GetType() == ECompartmentType.PILOT) return "driver";
  if (BBB_Transport.CommanderSeat(slot) || slot.GetType() == ECompartmentType.TURRET) return "commander";
  return "crewman";
 }

 protected static string Name(IEntity vehicle)
 {
  if (!vehicle || !vehicle.GetPrefabData()) return "vehicle";
  return FilePath.StripExtension(FilePath.StripPath(vehicle.GetPrefabData().GetPrefabName()));
 }

 // ------------------------------------------------------------------ orders to the gunner

 // The gates every order passes (BBB_HotAI.FireControl has checked hold fire, the turret and his gun).
 static BBB_CrewOrder OrderFor(SCR_AIUtilityComponent utility, IEntity man)
 {
  if (!On() || BBB.PlayerOf(man)) return null;
  IEntity vehicle = BBB_Transport.VehicleOf(man);
  if (!Vehicle.Cast(vehicle) || (BBB.s_Hooks && BBB.s_Hooks.LeaveAlone(vehicle))) return null;
  if (BBB.HasPlayer(BBB_HeldGuns.GroupOf(man)) || BBB_OrderIntent.GunnerBlocks(utility)) return null;
  BBB_CrewOrder order = Get(vehicle, man);
  if (!order || !order.m_bHas) return null;
  return order;
 }

 // He sees nothing of his own: where the leader wants his fire.
 static bool Aim(SCR_AIUtilityComponent utility, IEntity man, out vector aim, out int men, out float spread)
 {
  BBB_CrewOrder order = OrderFor(utility, man);
  if (!order) return false;
  aim = order.m_vAim;
  men = order.m_iMen;
  spread = order.m_fSpread;
  return true;
 }

 // He is fighting a target he can see: switch him, or break his fight off, when the leader's is worth more.
 static void Command(SCR_AIUtilityComponent utility, IEntity man, BaseTarget current, float nowMs)
 {
  SCR_AICombatComponent combat = utility.m_CombatComponent;
  if (!combat || !current) return;
  BBB_CrewOrder order = OrderFor(utility, man);
  if (!order) return;
  IEntity mine = current.GetTargetEntity();
  if (order.m_aMen.Contains(mine))
  {
   combat.BBBLeaderRenew(mine, nowMs); // on it: kept on it while it stays the leader's target
   return;
  }
  float his = order.ScoreOf(mine, current.GetLastSeenPosition());
  float ratio = order.m_fScore / Math.Max(his, 1);
  if (ratio < SWITCH_RATIO) return;
  // He can see one of the leader's men: onto him.
  BaseTarget seen = Visible(utility, order);
  if (seen)
  {
   if (combat.BBBLeaderWants(seen.GetTargetEntity(), nowMs, false))
    BBB_Think.NoteUtility(utility, "CrewLeader", "switched onto the leader's target: " + order.m_sWhat);
   return;
  }
  // He can't: the commander lays him on it.
  if (ratio < OVERRIDE_RATIO || current.GetDistance() < OVERRIDE_MIN_M || nowMs < utility.m_fBBBOverrideNext) return;
  if (BBB_HotAI.FireSeries(utility, man, order.m_vAim, order.m_iMen, order.m_fSpread, "the crew leader's target (" + order.m_sWhat + "), off a lesser one", order.m_Vehicle))
  {
   utility.m_fBBBOverrideNext = nowMs + OVERRIDE_EVERY_S * 1000;
   s_iOverrides++;
  }
 }

 // His own perception's target for one of the leader's men, in sight now.
 protected static BaseTarget Visible(SCR_AIUtilityComponent utility, BBB_CrewOrder order)
 {
  if (!utility.m_PerceptionComponent) return null;
  s_aTargets.Clear();
  utility.m_PerceptionComponent.GetTargetsList(s_aTargets, ETargetCategory.ENEMY);
  BaseTarget best;
  float bestDistance = float.MAX;
  foreach (BaseTarget target : s_aTargets)
  {
   if (!target || target.IsDisarmed() || !order.m_aMen.Contains(target.GetTargetEntity())) continue;
   if (!utility.m_CombatComponent.IsTargetVisible(target)) continue;
   float distance = target.GetDistance();
   if (distance >= bestDistance) continue;
   best = target;
   bestDistance = distance;
  }
  s_aTargets.Clear();
  return best;
 }

 // A commanded fire series ends when he sees one of the leader's men, or someone shooting at him.
 static bool OnTarget(IEntity vehicle, IEntity enemy)
 {
  if (!vehicle || !enemy || !s_mOrders) return false;
  BBB_CrewOrder order = s_mOrders.Get(vehicle);
  return order && order.m_bHas && order.m_aMen.Contains(enemy);
 }
}

modded class SCR_AIUtilityComponent
{
 float m_fBBBOverrideNext; // world time (ms) the crew leader may break off his fight again
}

// The leader's switch (crew leader, squad leader): after vanilla's own pick, his target becomes the man
// the leader wants, with the weapon his picker gives against that kind of target, and is held while the
// leader renews it.
modded class SCR_AICombatComponent
{
 protected IEntity m_BBBLeaderWant;
 protected float m_fBBBLeaderWantUntil;
 protected BaseTarget m_BBBLeaderHeld;
 protected float m_fBBBLeaderHeldUntil;
 protected bool m_bBBBLeaderYields; // infantry: the hold gives way when someone else shoots at him

 // From BBB_CrewLeader.Command / BBB_SquadLeader: true when this is a new switch. yields: the hold
 // ends when another enemy shoots at him (a man on foot answers that himself; a crew under armour
 // leaves it to its commander).
 bool BBBLeaderWants(IEntity enemy, float nowMs, bool yields)
 {
  if (m_BBBLeaderHeld && m_BBBLeaderHeld.GetTargetEntity() == enemy && nowMs < m_fBBBLeaderHeldUntil) return false;
  bool fresh = m_BBBLeaderWant != enemy;
  m_bBBBLeaderYields = yields;
  m_BBBLeaderWant = enemy;
  m_fBBBLeaderWantUntil = nowMs + 1500;
  m_fNextWeaponTargetEvaluation_ms = 0; // pick now, not in up to half a second
  return fresh;
 }

 // He is on the leader's target: hold it a while longer (only a target the leader switched him to).
 void BBBLeaderRenew(IEntity enemy, float nowMs)
 {
  if (m_BBBLeaderHeld && m_BBBLeaderHeld.GetTargetEntity() == enemy) m_fBBBLeaderHeldUntil = nowMs + BBB_CrewLeader.HOLD_S * 1000;
 }

 bool BBBLeaderPending()
 {
  return m_BBBLeaderWant && GetGame().GetWorld().GetWorldTime() < m_fBBBLeaderWantUntil;
 }

 // Hold the target the leader gave him while he can see it and the leader keeps renewing it.
 protected bool BBBLeaderHolding()
 {
  if (!m_BBBLeaderHeld || m_BBBLeaderHeld != GetCurrentTarget()) return false;
  if (GetGame().GetWorld().GetWorldTime() > m_fBBBLeaderHeldUntil || m_BBBLeaderHeld.IsDisarmed() || m_BBBLeaderHeld.GetTimeSinceSeen() > BBB_HotAI.HOLD_LOST_S)
  {
   m_BBBLeaderHeld = null;
   return false;
  }
  BaseWeaponComponent weapon = GetCurrentWeapon();
  if (!weapon || !weapon.GetCurrentMagazine() || weapon.GetCurrentMagazine().GetAmmoCount() <= 0) return false;
  // On foot and under fire: his own picker answers it.
  if (m_bBBBLeaderYields && BBB_SquadLeader.UnderFire(m_Utility))
  {
   m_BBBLeaderHeld = null;
   return false;
  }
  return true;
 }

 override void EvaluateWeaponAndTarget(out bool outWeaponEvent, out bool outSelectedTargetChanged,
  out BaseTarget outPrevTarget, out BaseTarget outCurrentTarget,
  out bool outRetreatTargetChanged, out bool outCompartmentChanged)
 {
  if (!BBBLeaderPending() && BBBLeaderHolding())
  {
   outWeaponEvent = false;
   outSelectedTargetChanged = false;
   return;
  }
  float stamp = m_fNextWeaponTargetEvaluation_ms;
  super.EvaluateWeaponAndTarget(outWeaponEvent, outSelectedTargetChanged, outPrevTarget, outCurrentTarget, outRetreatTargetChanged, outCompartmentChanged);
  if (stamp == m_fNextWeaponTargetEvaluation_ms || !BBBLeaderPending()) return;
  BBBLeaderSwitch(outWeaponEvent, outSelectedTargetChanged, outPrevTarget, outCurrentTarget);
 }

 protected void BBBLeaderSwitch(out bool weaponEvent, out bool targetChanged, out BaseTarget prevTarget, out BaseTarget currentTarget)
 {
  IEntity want = m_BBBLeaderWant;
  m_BBBLeaderWant = null;
  float now = GetGame().GetWorld().GetWorldTime();
  if (m_SelectedTarget && m_SelectedTarget.GetTargetEntity() == want)
  {
   m_BBBLeaderHeld = m_SelectedTarget;
   m_fBBBLeaderHeldUntil = now + BBB_CrewLeader.HOLD_S * 1000;
   return;
  }
  if (!m_Perception || !m_WeaponTargetSelector) return;
  BaseTarget target;
  array<BaseTarget> enemies = {};
  m_Perception.GetTargetsList(enemies, ETargetCategory.ENEMY);
  foreach (BaseTarget candidate : enemies)
  {
   if (candidate.GetTargetEntity() != want || candidate.IsDisarmed() || !IsTargetVisible(candidate)) continue;
   target = candidate;
   break;
  }
  if (!target) return;
  // The weapon his picker gives against this kind of target at this range.
  bool turret = m_AIInfo && m_AIInfo.HasUnitState(EUnitState.IN_TURRET);
  // On foot: his gun, never a grenade or a launcher (their own features decide those throws and shots).
  array<int> notThese;
  if (!turret) notThese = {EWeaponType.WT_FRAGGRENADE, EWeaponType.WT_SMOKEGRENADE, EWeaponType.WT_ROCKETLAUNCHER, EWeaponType.WT_GRENADELAUNCHER};
  if (!m_WeaponTargetSelector.SelectWeaponAgainstUnitTypeAndDistance(target.GetUnitType(), target.GetDistance(), true, turret, null, notThese)) return;
  BaseWeaponComponent weapon;
  int muzzle;
  BaseMagazineComponent magazine;
  m_WeaponTargetSelector.GetSelectedWeapon(weapon, muzzle, magazine);
  if (!weapon || BBB_Armour.Useless(target, magazine)) return;
  if (weapon != m_SelectedWeaponComp || muzzle != m_iSelectedMuzzle)
  {
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   EMuzzleType muzzleType;
   if (muzzle >= 0 && muzzle < muzzles.Count()) muzzleType = muzzles[muzzle].GetMuzzleType();
   m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(weapon.GetWeaponType(), muzzleType);
  }
  if (weapon != m_SelectedWeaponComp || muzzle != m_iSelectedMuzzle || magazine != m_SelectedMagazineComp) weaponEvent = true;
  m_SelectedWeaponComp = weapon;
  m_iSelectedMuzzle = muzzle;
  m_SelectedMagazineComp = magazine;
  m_WeaponTargetSelector.GetSelectedWeaponProperties(m_fSelectedWeaponMinDist, m_fSelectedWeaponMaxDist, m_bSelectedWeaponDirectDamage);
  if (!targetChanged) prevTarget = m_SelectedTarget;
  m_SelectedTarget = target;
  m_SelectedTargetVisible = false;
  m_SelectedTargetDestinationPos = vector.Zero;
  currentTarget = target;
  targetChanged = true;
  m_BBBLeaderHeld = target;
  m_fBBBLeaderHeldUntil = now + BBB_CrewLeader.HOLD_S * 1000;
  // (BBB_HotAI's override, compiled after this one, wraps it and restarts its turret hold on the change.)
  if (turret)
  {
   BBB_CrewLeader.s_iSwitches++;
   PrintFormat("[BBB] CREW LEADER gunner switched onto the leader's target %1 m away", Math.Round(target.GetDistance()));
  }
  else BBB_SquadLeader.s_iSwitches++;
 }
}
