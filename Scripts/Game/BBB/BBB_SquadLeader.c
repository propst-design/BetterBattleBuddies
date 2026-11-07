// Squad leader, stages 1 and 2 (Jacob, 7 Nov 2026: "squad leader now?" after the crew leader; design in
// the HANDBOOK, Planned features). Vanilla's group brain gives each enemy cluster its own state and
// locks fireteams to it, but nobody decides where the squad's fire goes: every man shoots what his own
// picker likes, and a man who hasn't spotted the enemy his mates are fighting looks wherever he was
// looking. Once a second, for each AI squad in contact, the leader reads the squad picture (BBB_Squad:
// what every member perceives, real sightings only) and:
//  1. Cue (-bbbSquadCue): a man who knows nothing of the enemy group he should be fighting, and is
//     fighting nothing he can see, is told where it is: he turns to look (vanilla's commander look
//     priority) and spots it himself. No aim point is handed over.
//  2. Fire (-bbbSquadFire): the picture's sightings are grouped (GROUP_M) and scored like the crew
//     leader's (BBB_CrewLeader.ThreatValue: men, launcher men, vehicles, shooting at us, less with
//     range). The best group is the main threat. A second group on another front (>= FRONT_DEG off,
//     worth >= SPLIT_SHARE of the main; kept while >= KEEP_SHARE) splits the squad: whole vanilla
//     fireteams nearest that group take it, about its share of the men (SHARE_MIN..SHARE_MAX), machine
//     gunners stay on the main threat. The second group stays in the leader's mind SPLIT_MEMORY_S after
//     anyone last saw it (at its real last seen spots): a team that went to ground under fire aged out
//     of a 20 s picture and the split ended at 35 s (test run 4). A man who sees one of his group's men while fighting someone
//     else is switched onto it after vanilla's own pick (BBB_CrewLeader's switch, his gun only, no
//     grenades or launchers) and kept on it while he stays on it; with no split, only when the main
//     threat is worth SWITCH_RATIO more than what he is fighting. In a split, a man fighting the other
//     group who can't see his own gets a short laid burst (BBB_AILeaderFireBehavior, LAY_S, priority
//     LAY_PRIORITY: above a selected attack, below a high-priority one and every danger reaction) into
//     a box on a real sighting of it, so he turns that way; it ends when he sees one of its men (then
//     the switch), comes under fire or has an enemy within SELF_M. Once per LAY_EVERY_S per man.
//     The second team keeps to its sector: it doesn't attack the main group (attack priority 0) unless
//     that enemy is within SELF_M or the man is under fire, and between bursts watches its own group
//     (cue). Without that, vanilla's picker (nearer, "endangering") pulled it back onto the main group
//     whenever the second group went to ground (test runs 4-5).
//     His own fight, untouched: an enemy within SELF_M, or while he is under fire (his suppression
//     >= UNDER_FIRE; the hold also gives way to that: infantry have no armour). Not vanilla's
//     "endangered" flag: hold-fire enemies set it just by aiming, and it blocked every switch (test run 3).
//  The leader calls a split out (vanilla REPORT_TARGET on the second group).
// Who: AI squads on foot with vanilla's group perception in contact (its most dangerous cluster),
// not player-led (AiO's), host intent not NONE; per man: AI, conscious, on foot, not on a player or
// Game Master order (BBB_OrderIntent.Blocks), not committed to a rescue, gun or ammo run
// (BBB_Claims.Committed), squad not on hold fire. A squad reorganising after losing its leader
// (BBB_Tactic REORG) gets cues but no new split. Nothing here moves anyone.
class BBB_SquadGroup : Managed
{
 IEntity m_Key;        // the man whose spot is the group's aim
 vector m_vAim;
 float m_fScore;
 float m_fBearing;     // from the squad centre
 int m_iMen;
 ref array<IEntity> m_aMen = {};
}

class BBB_SquadOrders : Managed
{
 SCR_AIGroup m_Group;
 float m_fLook;                  // world s of the last look
 bool m_bSplit;
 float m_fSplitBearing;          // where the second group is (kept current): the team keeps that group
 ref array<AIAgent> m_aSecond = {}; // men on the second group
 int m_iConscious;               // when the teams were drawn
 float m_fSaidAt;
 ref BBB_SquadGroup m_Main;
 ref BBB_SquadGroup m_Other;
}

class BBB_SquadLeader
{
 static bool s_bCue;
 static bool s_bFire;
 protected static int s_iRead;
 protected static ref map<SCR_AIGroup, ref BBB_SquadOrders> s_mOrders;
 protected static ref array<SCR_AIGroup> s_aGroups;
 protected static ref array<AIAgent> s_aAgents;
 protected static ref array<BaseTarget> s_aTargets;
 static ref array<SCR_AIGroup> s_aVanilla; // test only: squads left without a leader
 static int s_iCues;     // looks cued (tests and the log)
 static int s_iSplits;   // squads split onto two groups
 static int s_iSwitches; // men switched onto their group (counted in the switch itself)
 static int s_iSaid;

 static const float LOOK_S = 1;
 static const float RECENT_S = 20;
 static const float SPLIT_MEMORY_S = 45; // a split's second group, gone to ground, is remembered this long
 static const float GROUP_M = 25;
 static const float FRONT_DEG = 35;
 static const float SPLIT_SHARE = 0.4;
 static const float KEEP_SHARE = 0.25;
 static const float SHARE_MIN = 0.3;
 static const float SHARE_MAX = 0.5;
 static const float SWITCH_RATIO = 1.5;
 static const float SELF_M = 30;
 static const float UNDER_FIRE = 0.3; // his own suppression (fly-bys, blasts): being shot at
 static const float KNOWS_S = 4;     // a man "knows" an enemy he has detected within this
 static const float CUE_EVERY_S = 3;
 static const float SAY_EVERY_S = 15;
 static const int MIN_SPLIT_MEN = 4;
 static const int MIN_SQUAD = 2;       // a lone man has no squad to lead: his own picker decides
 static const float LAY_S = 4;
 static const float LAY_EVERY_S = 12;
 static const float LAY_PRIORITY = 95;
 static int s_iLaid;
 static int s_iHeldBack; // attacks held back by the second team's sector (evaluations, tests)

 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_mOrders = new map<SCR_AIGroup, ref BBB_SquadOrders>();
  s_aGroups = {};
  s_aAgents = {};
  s_aTargets = {};
  s_aVanilla = {};
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bCue = !(BBB_Settings.Get("bbbSquadCue", value) && value == "0");
  s_bFire = !(BBB_Settings.Get("bbbSquadFire", value) && value == "0");
  PrintFormat("[BBB] SQUAD LEADER cue=%1 fire=%2", s_bCue, s_bFire);
 }

 static void Stop()
 {
  if (s_mOrders) s_mOrders.Clear();
  if (s_aVanilla) s_aVanilla.Clear();
 }

 protected static float Now() { return GetGame().GetWorld().GetWorldTime() / 1000; }

 // From BBB.Tick, once a second.
 static void Tick()
 {
  Read();
  if ((!s_bCue && !s_bFire) || !BBB.s_Hooks || !s_mOrders) return;
  float now = Now();
  s_aGroups.Clear();
  BBB.s_Hooks.GetGroups(s_aGroups);
  foreach (SCR_AIGroup group : s_aGroups)
  {
   if (!InContact(group))
   {
    s_mOrders.Remove(group);
    continue;
   }
   BBB_SquadOrders orders = s_mOrders.Get(group);
   if (!orders)
   {
    orders = new BBB_SquadOrders();
    orders.m_Group = group;
    s_mOrders.Set(group, orders);
   }
   if (now - orders.m_fLook < LOOK_S) continue;
   orders.m_fLook = now;
   Look(orders, now);
  }
  s_aGroups.Clear();
  // Squads gone.
  for (int i = s_mOrders.Count() - 1; i >= 0; i--)
   if (!s_mOrders.GetKey(i)) s_mOrders.RemoveElement(i);
 }

 protected static bool InContact(SCR_AIGroup group)
 {
  if (!group || s_aVanilla.Contains(group) || BBB.HasPlayer(group)) return false;
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  if (!utility || !utility.m_Perception || !utility.m_Perception.m_MostDangerousCluster) return false;
  if (utility.GetCombatModeActual() == EAIGroupCombatMode.HOLD_FIRE) return false;
  vector centre;
  float radius;
  return BBB_HostIntent.Of(group, centre, radius) != BBB_EIntent.NONE;
 }

 // ------------------------------------------------------------------ the leader's look

 protected static void Look(BBB_SquadOrders orders, float now)
 {
  SCR_AIGroup group = orders.m_Group;
  BBB_SquadPicture picture = BBB_Squad.Get(group);
  // A one-man group's picture is his own eyes: leading it would only override his own picker (it
  // changed whom a lone enemy rifleman aimed at in the crew leader test: decoy samples 57 -> 0-9).
  if (!picture || !picture.m_bHasCentre || picture.m_iConscious < MIN_SQUAD) return;
  Groups(orders, picture, now);
  if (!orders.m_Main) return;
  if (s_bFire) Split(orders, picture, now);
  else orders.m_bSplit = false;

  s_aAgents.Clear();
  BBB.s_Hooks.GetMembers(group, s_aAgents);
  foreach (AIAgent agent : s_aAgents)
  {
   SCR_AIUtilityComponent utility = Able(agent);
   if (!utility) continue;
   BBB_SquadGroup mine = orders.m_Main;
   if (orders.m_bSplit && orders.m_Other && orders.m_aSecond.Contains(agent)) mine = orders.m_Other;
   Order(orders, utility, mine, now);
  }
  s_aAgents.Clear();
 }

 // An AI man on foot the leader can give orders to.
 protected static SCR_AIUtilityComponent Able(AIAgent agent)
 {
  if (!agent) return null;
  IEntity man = agent.GetControlledEntity();
  if (!man || BBB.PlayerOf(man) || !BBB.IsConscious(man) || BBB_Transport.VehicleOf(man)) return null;
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (!utility || !utility.m_CombatComponent || BBB_OrderIntent.Blocks(utility) || BBB_Claims.Committed(man)) return null;
  return utility;
 }

 // The picture's sightings (<= RECENT_S) in groups; the best is the main threat, the best on another
 // front the second.
 protected static void Groups(BBB_SquadOrders orders, BBB_SquadPicture picture, float now)
 {
  array<BBB_SquadContact> recent = {};
  array<float> values = {};
  foreach (BBB_SquadContact contact : picture.m_Contacts)
  {
   if (!contact.m_Enemy || contact.m_eType == EAIUnitType.UnitType_Aircraft) continue;
   bool remembered = orders.m_bSplit && contact.m_fAge <= SPLIT_MEMORY_S && BBB_Squad.AngleBetween(contact.m_fBearing, orders.m_fSplitBearing) < FRONT_DEG;
   if (contact.m_fAge > RECENT_S && !remembered) continue;
   recent.Insert(contact);
   values.Insert(BBB_CrewLeader.ThreatValue(contact.m_eType, contact.m_Enemy, contact.m_bShooting, now));
  }
  orders.m_Main = null;
  orders.m_Other = null;
  int count = recent.Count();
  if (count == 0) return;
  array<float> scores = {};
  int best = -1;
  float bestScore = -float.MAX;
  for (int i = 0; i < count; i++)
  {
   float score = -BBB_CrewLeader.RANGE_PENALTY * vector.DistanceXZ(picture.m_vCentre, recent[i].m_vPos);
   for (int j = 0; j < count; j++)
    if (vector.DistanceXZ(recent[i].m_vPos, recent[j].m_vPos) <= GROUP_M) score += values[j];
   scores.Insert(score);
   if (score > bestScore)
   {
    best = i;
    bestScore = score;
   }
  }
  orders.m_Main = MakeGroup(recent, scores, best);
  // The second: the best sighting on another front, not part of the main group.
  int other = -1;
  float otherScore = -float.MAX;
  for (int k = 0; k < count; k++)
  {
   if (orders.m_Main.m_aMen.Contains(recent[k].m_Enemy)) continue;
   if (BBB_Squad.AngleBetween(recent[k].m_fBearing, orders.m_Main.m_fBearing) < FRONT_DEG) continue;
   if (scores[k] <= otherScore) continue;
   other = k;
   otherScore = scores[k];
  }
  if (other >= 0) orders.m_Other = MakeGroup(recent, scores, other);
 }

 protected static BBB_SquadGroup MakeGroup(array<BBB_SquadContact> recent, array<float> scores, int key)
 {
  BBB_SquadGroup made = new BBB_SquadGroup();
  made.m_Key = recent[key].m_Enemy;
  made.m_vAim = recent[key].m_vPos;
  made.m_fScore = scores[key];
  made.m_fBearing = recent[key].m_fBearing;
  foreach (BBB_SquadContact contact : recent)
  {
   if (vector.DistanceXZ(recent[key].m_vPos, contact.m_vPos) > GROUP_M) continue;
   made.m_aMen.Insert(contact.m_Enemy);
   made.m_iMen++;
  }
  return made;
 }

 // Split the squad onto the second group, keep the split, or end it.
 protected static void Split(BBB_SquadOrders orders, BBB_SquadPicture picture, float now)
 {
  BBB_SquadGroup main = orders.m_Main;
  BBB_SquadGroup other = orders.m_Other;
  float share = 0;
  if (other && main.m_fScore > 0) share = other.m_fScore / main.m_fScore;
  bool reorg = BBB_Tactic.TacticOf(orders.m_Group) == BBB_Tactic.REORG;
  if (orders.m_bSplit && other)
  {
   // Two groups of about the same worth trade ranks from one look to the next: the second team keeps
   // the group on its bearing, whichever ranks first now (else it would be flipped every second).
   if (BBB_Squad.AngleBetween(main.m_fBearing, orders.m_fSplitBearing) < BBB_Squad.AngleBetween(other.m_fBearing, orders.m_fSplitBearing))
   {
    orders.m_Main = other;
    orders.m_Other = main;
    main = orders.m_Main;
    other = orders.m_Other;
   }
   share = 0;
   if (main.m_fScore > 0) share = other.m_fScore / main.m_fScore;
   if (main.m_fScore > 0 && other.m_fScore > main.m_fScore) share = 1;
   orders.m_fSplitBearing = other.m_fBearing;
  }
  if (orders.m_bSplit)
  {
   if (other && share >= KEEP_SHARE && picture.m_iConscious >= MIN_SPLIT_MEN)
   {
    // Same split while its men are still there; redraw the teams when men are lost.
    if (picture.m_iConscious != orders.m_iConscious) Teams(orders, picture);
    return;
   }
   orders.m_bSplit = false;
   orders.m_aSecond.Clear();
   PrintFormat("[BBB] SQUAD LEADER %1: back on one group", BBB.s_Hooks.NameOf(orders.m_Group));
   return;
  }
  if (!other || reorg || share < SPLIT_SHARE || picture.m_iConscious < MIN_SPLIT_MEN) return;
  orders.m_bSplit = true;
  orders.m_fSplitBearing = other.m_fBearing;
  Teams(orders, picture);
  s_iSplits++;
  PrintFormat("[BBB] SQUAD LEADER %1: splits fire, %2 men onto %3 enemies at %4 m (score %5, main %6 enemies at %7 m, score %8)", BBB.s_Hooks.NameOf(orders.m_Group), orders.m_aSecond.Count(), other.m_iMen, Math.Round(vector.DistanceXZ(picture.m_vCentre, other.m_vAim)), Math.Round(other.m_fScore), main.m_iMen, Math.Round(vector.DistanceXZ(picture.m_vCentre, main.m_vAim)), Math.Round(main.m_fScore));
  BBB_Think.NoteGroup(orders.m_Group, "SquadLeader", "splits fire: " + orders.m_aSecond.Count().ToString() + " men onto the second group");
  Say(orders, other, now);
 }

 // Whole vanilla fireteams nearest the second group, about its share of the men; machine gunners stay
 // on the main threat. Without fireteams, the men nearest the second group.
 protected static void Teams(BBB_SquadOrders orders, BBB_SquadPicture picture)
 {
  orders.m_aSecond.Clear();
  orders.m_iConscious = picture.m_iConscious;
  BBB_SquadGroup main = orders.m_Main;
  BBB_SquadGroup other = orders.m_Other;
  float share = Math.Clamp(other.m_fScore / Math.Max(main.m_fScore + other.m_fScore, 1), SHARE_MIN, SHARE_MAX);
  int want = Math.Round(share * picture.m_iConscious);
  if (want < 1) want = 1;
  // Candidates: able men, with their fireteam (vanilla's) and distance to the second group.
  array<AIAgent> men = {};
  array<int> teams = {};
  array<float> distances = {};
  s_aAgents.Clear();
  BBB.s_Hooks.GetMembers(orders.m_Group, s_aAgents);
  SCR_AIGroupUtilityComponent groupUtility = orders.m_Group.GetGroupUtilityComponent();
  foreach (AIAgent agent : s_aAgents)
  {
   SCR_AIUtilityComponent utility = Able(agent);
   if (!utility || MachineGunner(utility)) continue;
   int team = -1;
   if (groupUtility && groupUtility.m_FireteamMgr)
   {
    SCR_AIGroupFireteam fireteam = groupUtility.m_FireteamMgr.FindFireteam(agent);
    if (fireteam) team = groupUtility.m_FireteamMgr.GetFireteamId(fireteam);
   }
   men.Insert(agent);
   teams.Insert(team);
   distances.Insert(vector.DistanceXZ(agent.GetControlledEntity().GetOrigin(), other.m_vAim));
  }
  s_aAgents.Clear();
  // Fireteams by their mean distance to the second group, nearest first; men without one go alone.
  array<int> order = {};
  array<float> teamDistance = {};
  for (int i = 0; i < men.Count(); i++)
  {
   int id = teams[i];
   if (id < 0) id = 1000 + i;
   int at = order.Find(id);
   if (at < 0)
   {
    order.Insert(id);
    teamDistance.Insert(0);
    at = order.Count() - 1;
   }
   teamDistance[at] = teamDistance[at] + distances[i];
  }
  for (int t = 0; t < order.Count(); t++)
  {
   int members;
   for (int m = 0; m < men.Count(); m++)
   {
    int mid = teams[m];
    if (mid < 0) mid = 1000 + m;
    if (mid == order[t]) members++;
   }
   teamDistance[t] = teamDistance[t] / Math.Max(members, 1);
  }
  while (orders.m_aSecond.Count() < want && !order.IsEmpty())
  {
   int nearest = 0;
   for (int n = 1; n < order.Count(); n++)
    if (teamDistance[n] < teamDistance[nearest]) nearest = n;
   int pick = order[nearest];
   for (int p = 0; p < men.Count(); p++)
   {
    int pid = teams[p];
    if (pid < 0) pid = 1000 + p;
    if (pid == pick && orders.m_aSecond.Count() < want + 1) orders.m_aSecond.Insert(men[p]);
   }
   order.Remove(nearest);
   teamDistance.Remove(nearest);
  }
  // Never the whole squad: at least one man stays on the main threat besides the machine gunners.
  if (orders.m_aSecond.Count() >= men.Count() && men.Count() > 1) orders.m_aSecond.Remove(orders.m_aSecond.Count() - 1);
 }

 protected static bool MachineGunner(SCR_AIUtilityComponent utility)
 {
  BaseWeaponComponent weapon = utility.m_CombatComponent.GetCurrentWeapon();
  return weapon && weapon.GetWeaponType() == EWeaponType.WT_MACHINEGUN;
 }

 // ------------------------------------------------------------------ orders to one man

 protected static void Order(BBB_SquadOrders orders, SCR_AIUtilityComponent utility, BBB_SquadGroup mine, float now)
 {
  SCR_AICombatComponent combat = utility.m_CombatComponent;
  BaseTarget current = combat.GetCurrentTarget();
  bool fighting = current && combat.IsTargetVisible(current);
  float nowMs = now * 1000;
  if (fighting)
  {
   IEntity on = current.GetTargetEntity();
   if (mine.m_aMen.Contains(on))
   {
    combat.BBBLeaderRenew(on, nowMs); // on his group: kept on it
    return;
   }
   if (!s_bFire) return;
   // His own fight: close, or he is being shot at.
   if (current.GetDistance() < SELF_M || UnderFire(utility)) return;
   // No split: the main threat must clearly outweigh what he is fighting.
   if (!orders.m_bSplit && mine.m_fScore < SWITCH_RATIO * Math.Max(ScoreOf(orders, on, current), 1)) return;
   BaseTarget seen = Visible(utility, mine);
   if (seen)
   {
    if (combat.BBBLeaderWants(seen.GetTargetEntity(), nowMs, true))
     BBB_Think.NoteUtility(utility, "SquadLeader", "onto the leader's target: " + mine.m_iMen.ToString() + " enemies");
    return;
   }
   // Split, and he can't see his group from where he aims: a short burst there turns him, and
   // between bursts a man of the second team (held back from the main group) watches his own.
   if (!orders.m_bSplit) return;
   if (now >= utility.m_fBBBLayNext) Lay(orders, utility, mine, now);
   else if (mine == orders.m_Other) Cue(utility, mine, now);
   return;
  }
  // Fighting nothing he can see: if he doesn't know his group, the leader shows him where it is.
  if (Knows(utility, mine)) return;
  Cue(utility, mine, now);
 }

 protected static void Cue(SCR_AIUtilityComponent utility, BBB_SquadGroup mine, float now)
 {
  if (!s_bCue || now < utility.m_fBBBCueNext || !utility.m_LookAction) return;
  utility.m_fBBBCueNext = now + CUE_EVERY_S;
  utility.m_LookAction.LookAt(mine.m_vAim + Vector(0, 1, 0), SCR_AILookAction.PRIO_COMMANDER, 2);
  s_iCues++;
  BBB_Think.NoteUtility(utility, "SquadLeader", "told where the enemy is: looks " + Math.Round(vector.DistanceXZ(utility.m_OwnerEntity.GetOrigin(), mine.m_vAim)).ToString() + " m off");
 }

 protected static void Lay(BBB_SquadOrders orders, SCR_AIUtilityComponent utility, BBB_SquadGroup mine, float now)
 {
  utility.m_fBBBLayNext = now + LAY_EVERY_S;
  vector aim = BBB.Ground(mine.m_vAim);
  float distance = vector.DistanceXZ(utility.m_OwnerEntity.GetOrigin(), aim);
  if (distance < SELF_M || !BBB_SuppressionSafety.GroundSector(aim)) return;
  float half = Math.Clamp(distance * 0.025, 2.5, 8);
  SCR_AISuppressionVolumeBox volume = new SCR_AISuppressionVolumeBox(Vector(aim[0] - half, aim[1] - 0.5, aim[2] - half), Vector(aim[0] + half, aim[1] + 2, aim[2] + half));
  BBB_AILeaderFireBehavior fire = new BBB_AILeaderFireBehavior(utility, null, volume, LAY_S, 1);
  fire.m_Squad = orders.m_Group;
  utility.AddAction(fire);
  s_iLaid++;
  BBB_Think.NoteUtility(utility, "SquadLeader", "sent onto the other group: a burst toward it, " + Math.Round(distance).ToString() + " m");
 }

 // Does he see a man of the group his leader gave him (BBB_AILeaderFireBehavior ends then)?
 static bool SeesHisGroup(SCR_AIGroup group, SCR_AIUtilityComponent utility)
 {
  if (!s_mOrders || !group || !utility) return false;
  BBB_SquadOrders orders = s_mOrders.Get(group);
  if (!orders || !orders.m_Main) return false;
  BBB_SquadGroup mine = orders.m_Main;
  AIAgent agent = utility.GetAIAgent();
  if (orders.m_bSplit && orders.m_Other && orders.m_aSecond.Contains(agent)) mine = orders.m_Other;
  return Visible(utility, mine) != null;
 }

 // The second team's sector: should this attack on that target wait? Cached half a second per man.
 static bool HoldsBack(SCR_AIUtilityComponent utility, BaseTarget target)
 {
  if (!utility || !target || !s_bFire || !s_mOrders) return false;
  float now = Now();
  if (now < utility.m_fBBBSectorUntil && target.GetTargetEntity() == utility.m_BBBSectorTarget) return utility.m_bBBBSectorHold;
  utility.m_fBBBSectorUntil = now + 0.5;
  utility.m_BBBSectorTarget = target.GetTargetEntity();
  utility.m_bBBBSectorHold = false;
  AIAgent agent = utility.GetAIAgent();
  if (!agent) return false;
  SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
  BBB_SquadOrders orders = s_mOrders.Get(group);
  if (!orders || !orders.m_bSplit || !orders.m_Other || !orders.m_Main || !orders.m_aSecond.Contains(agent)) return false;
  if (!orders.m_Main.m_aMen.Contains(target.GetTargetEntity()) || orders.m_Other.m_aMen.Contains(target.GetTargetEntity())) return false;
  if (target.GetDistance() < SELF_M || UnderFire(utility) || BBB_OrderIntent.Blocks(utility)) return false;
  utility.m_bBBBSectorHold = true;
  return true;
 }

 static bool UnderFire(SCR_AIUtilityComponent utility)
 {
  return utility && utility.m_ThreatSystem && utility.m_ThreatSystem.GetSuppressionMeasure() >= UNDER_FIRE;
 }

 // The score of the group this enemy is in (main or second), else of him alone where he was seen.
 protected static float ScoreOf(BBB_SquadOrders orders, IEntity enemy, BaseTarget target)
 {
  if (orders.m_Main && orders.m_Main.m_aMen.Contains(enemy)) return orders.m_Main.m_fScore;
  if (orders.m_Other && orders.m_Other.m_aMen.Contains(enemy)) return orders.m_Other.m_fScore;
  return BBB_CrewLeader.MAN - BBB_CrewLeader.RANGE_PENALTY * target.GetDistance();
 }

 // Has he detected any man of this group lately?
 protected static bool Knows(SCR_AIUtilityComponent utility, BBB_SquadGroup group)
 {
  if (!utility.m_PerceptionComponent) return true;
  s_aTargets.Clear();
  utility.m_PerceptionComponent.GetTargetsList(s_aTargets, ETargetCategory.ENEMY);
  bool knows;
  foreach (BaseTarget target : s_aTargets)
  {
   if (!target || !group.m_aMen.Contains(target.GetTargetEntity())) continue;
   // Seen or heard lately (the detected time alone may not follow what he is looking at).
   if (target.GetTimeSinceSeen() > KNOWS_S && target.GetTimeSinceDetected() > KNOWS_S) continue;
   knows = true;
   break;
  }
  s_aTargets.Clear();
  return knows;
 }

 // His own perception's target for a man of this group, in sight now (the nearest).
 protected static BaseTarget Visible(SCR_AIUtilityComponent utility, BBB_SquadGroup group)
 {
  if (!utility.m_PerceptionComponent) return null;
  s_aTargets.Clear();
  utility.m_PerceptionComponent.GetTargetsList(s_aTargets, ETargetCategory.ENEMY);
  BaseTarget best;
  float bestDistance = float.MAX;
  foreach (BaseTarget target : s_aTargets)
  {
   if (!target || target.IsDisarmed() || !group.m_aMen.Contains(target.GetTargetEntity())) continue;
   if (!utility.m_CombatComponent.IsTargetVisible(target)) continue;
   float distance = target.GetDistance();
   if (distance >= bestDistance) continue;
   best = target;
   bestDistance = distance;
  }
  s_aTargets.Clear();
  return best;
 }

 // The leader (or the next conscious man) calls the second group out: vanilla's "target" line.
 protected static void Say(BBB_SquadOrders orders, BBB_SquadGroup group, float now)
 {
  if (now - orders.m_fSaidAt < SAY_EVERY_S || !group.m_Key) return;
  IEntity leader = orders.m_Group.GetLeaderEntity();
  if (!BBB.IsConscious(leader) || BBB.PlayerOf(leader)) return;
  SCR_AIUtilityComponent speaker = BBB_VehiclePosture.Utility(leader);
  if (!speaker || !speaker.m_CommsHandler || speaker.m_CommsHandler.CanBypass()) return;
  speaker.m_CommsHandler.AddRequest(new SCR_AITalkRequest(ECommunicationType.REPORT_TARGET, group.m_Key, group.m_vAim, 0, true, false, SCR_EAITalkRequestPreset.MEDIUM));
  orders.m_fSaidAt = now;
  s_iSaid++;
 }

 // Tests: is this man on the second group?
 static bool OnSecond(SCR_AIGroup group, AIAgent agent)
 {
  if (!s_mOrders || !group) return false;
  BBB_SquadOrders orders = s_mOrders.Get(group);
  return orders && orders.m_bSplit && orders.m_aSecond.Contains(agent);
 }
}

modded class SCR_AIUtilityComponent
{
 float m_fBBBCueNext; // world s the squad leader may cue his look again
 float m_fBBBLayNext; // world s the squad leader may send a burst onto his group again
 float m_fBBBSectorUntil;   // HoldsBack cache
 IEntity m_BBBSectorTarget;
 bool m_bBBBSectorHold;
}

// The second team keeps to its sector (BBB_SquadLeader.HoldsBack): an attack on the main group waits.
modded class SCR_AIAttackBehavior
{
 override float CustomEvaluate()
 {
  float priority = super.CustomEvaluate();
  if (priority <= 0 || !BBB_SquadLeader.HoldsBack(m_Utility, m_Target.m_Value)) return priority;
  BBB_SquadLeader.s_iHeldBack++;
  return 0;
 }
}

// The squad leader's laid burst: vanilla suppressive fire into a box on a real sighting of the group
// he was given, until he sees one of its men, comes under fire, meets an enemy within SELF_M, or LAY_S.
class BBB_AILeaderFireBehavior : SCR_AISuppressBehavior
{
 protected float m_fEndMs;
 SCR_AIGroup m_Squad;

 // Same leading parameters as vanilla's (Enforce passes them to the parent constructor).
 void BBB_AILeaderFireBehavior(SCR_AIUtilityComponent utility, SCR_AIActivityBase groupActivity, SCR_AISuppressionVolumeBase suppressionVolume, float suppressionDuration, float fireRate, float priorityLevel = PRIORITY_LEVEL_NORMAL)
 {
  m_fEndMs = GetGame().GetWorld().GetWorldTime() + suppressionDuration * 1000;
  SetIsUniqueInActionQueue(true);
 }

 override float CustomEvaluate()
 {
  if (GetGame().GetWorld().GetWorldTime() > m_fEndMs || !m_Utility || !m_Utility.m_CombatComponent) { Complete(); return 0; }
  if (m_Utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE || BBB_OrderIntent.Blocks(m_Utility)) { Complete(); return 0; }
  if (BBB_SquadLeader.UnderFire(m_Utility) || BBB_SquadLeader.SeesHisGroup(m_Squad, m_Utility)) { Complete(); return 0; }
  BaseTarget target = m_Utility.m_CombatComponent.GetCurrentTarget();
  if (target && target.GetDistance() < BBB_SquadLeader.SELF_M) { Complete(); return 0; }
  return BBB_SquadLeader.LAY_PRIORITY;
 }
}
