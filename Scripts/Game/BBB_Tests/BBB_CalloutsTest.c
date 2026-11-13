// Squad leader callouts (-bbbCalloutsTest 1, Test-BBB.ps1 -Preset callouts; 12 Nov 2026). Audio can't be
// heard on the hidden server, so the test reads the line ids BBB_Callouts decided and the ones the comms
// handler actually transmitted (modded TransmitRequest), per squad.
// Fixture: on a clear lane, US rifle squad A (AI leader) and, 300 m to the side, squad B whose leader is
// treated as a player (BBB_Callouts.s_TestPlayer). Observers at both keep them at full detail (LOD 0) so
// requests are transmitted, not bypassed. Everyone invulnerable except the men put down.
// Timeline (s): 8 A move order, B suppress order | 22 A suppress order | 36/39/42 three A move orders 3 s
// apart (cooldown) | 48 a USSR team (hold fire) 150 m up A's lane (contact) | 76 an A rifleman and a B
// rifleman down, 80 a second A rifleman down | 96 judged.
// Judged (11): spawned; A answered the move (leader's line or a man's "roger"); A's suppress order voiced
// by the leader; something actually transmitted; three orders 3 s apart drew one answer; contact called
// once within its cooldown; man down; another man down; B (player leader) had no BBB line at all though
// it got an order and a man down; the player gate was consulted for B; no BBB line from a player's mouth.
class BBB_CalloutsTest : BBB_AimTest
{
 static ref BBB_CalloutsTest s_Callouts;
 static const ResourceName SQUAD_US = "{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et";
 static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 static const ResourceName WP_SUPPRESS = "{ED8277F35B46B4AA}Prefabs/AI/Waypoints/AIWaypoint_Suppress.et";
 static const int EXPECTED = 11;
 static const int OBSERVER_A = 22421;
 static const int OBSERVER_B = 22422;

 protected int m_iChecks;
 protected int m_iFailures;
 protected int m_iT;
 protected vector m_vFrom;
 protected vector m_vDir;
 protected vector m_vSide;
 protected SCR_AIGroup m_A;
 protected SCR_AIGroup m_B;
 protected SCR_AIGroup m_Enemy;
 protected ref array<IEntity> m_aA;
 protected ref array<IEntity> m_aB;
 protected ref array<IEntity> m_aEnemy;
 protected ref map<string, int> m_mMark;
 protected int m_iSaid0;
 protected int m_iHeld0;
 protected int m_iSaidByPlayer0;
 protected int m_iOrderAnswers;
 protected int m_iOrderLeader;
 protected int m_iCooldownAck;
 protected int m_iCooldownOrder;
 protected int m_iContact;

 static void MaybeStartCallouts()
 {
  if (s_Callouts || !Replication.IsServer() || !System.IsCLIParam("bbbCalloutsTest")) return;
  s_Callouts = new BBB_CalloutsTest();
  GetGame().GetCallqueue().CallLater(s_Callouts.Begin, 12000);
 }

 override protected void Check(string name, bool ok, string detail)
 {
  m_iChecks++;
  string res = "PASS";
  if (!ok)
  {
   res = "FAIL";
   m_iFailures++;
  }
  PrintFormat("[BBB] CALLOUTSTEST CHECK %1 %2 %3", name, res, detail);
 }

 override protected void Begin()
 {
  m_aA = {};
  m_aB = {};
  m_aEnemy = {};
  m_mMark = new map<string, int>();
  m_vFrom = BBB.Ground(Vector(1340, 0, 2770));
  m_vDir = "0 0 1";
  if (FindSite())
  {
   m_vFrom = BBB.Ground(m_aSiteFrom[0]);
   m_vDir = m_aSiteDir[0];
   LoadLane();
  }
  m_vSide = Vector(m_vDir[2], 0, -m_vDir[0]);
  vector bAt = BBB.Ground(m_vFrom + m_vSide * 300);
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers)
  {
   observers.InsertObserverSP(OBSERVER_A, m_vFrom[0], m_vFrom[2], null);
   observers.InsertObserverSP(OBSERVER_B, bAt[0], bAt[2], null);
  }
  BBB_Callouts.Read();
  BBB_Callouts.s_bLog = true;
  m_A = SCR_AIGroup.Cast(Spawn(SQUAD_US, m_vFrom, m_vDir));
  m_B = SCR_AIGroup.Cast(Spawn(SQUAD_US, bAt, m_vDir));
  PrintFormat("[BBB] CALLOUTSTEST lane at %1 heading %2, squad B at %3; callouts on %4 rate %5", m_vFrom, m_vDir, bAt, BBB_Callouts.s_bOn, BBB_Callouts.s_fRate);
  GetGame().GetCallqueue().CallLater(Step, 1000, true);
 }

 protected int Collect(SCR_AIGroup group, array<IEntity> into)
 {
  into.Clear();
  if (!group) return 0;
  PinDetail(group);
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body) continue;
   PinDetail(agent);
   body.GetDamageManager().EnableDamageHandling(false);
   into.Insert(body);
  }
  return into.Count();
 }

 // Replace the group's orders with one new waypoint (as a commander or GM giving a new order).
 protected void Order(SCR_AIGroup group, ResourceName prefab, vector at)
 {
  if (!group) return;
  array<AIWaypoint> old = {};
  group.GetWaypoints(old);
  foreach (AIWaypoint waypoint : old)
   group.RemoveWaypoint(waypoint);
  AIWaypoint order = AIWaypoint.Cast(Spawn(prefab, at, m_vDir));
  if (order) group.AddWaypoint(order);
 }

 // A rifleman of the group (not the leader) put down for good.
 protected void Down(SCR_AIGroup group, array<IEntity> men, int skip)
 {
  if (!group) return;
  IEntity leader = group.GetLeaderEntity();
  int seen;
  foreach (IEntity man : men)
  {
   if (!man || man == leader || !BBB.IsAlive(man)) continue;
   if (seen++ < skip) continue;
   ChimeraCharacter body = ChimeraCharacter.Cast(man);
   SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(body.GetDamageManager());
   if (!damage) continue;
   damage.EnableDamageHandling(true);
   damage.Kill(Instigator.CreateInstigator(null));
   PrintFormat("[BBB] CALLOUTSTEST t=%1 %2 down (squad %3)", m_iT, man, group);
   return;
  }
 }

 // Lines squad A's men decided.
 protected int Lines(string line)
 {
  return BBB_Callouts.LinesOf(m_A, line);
 }

 protected void Mark(string key, string line)
 {
  m_mMark.Set(key, Lines(line));
 }

 protected int Since(string key, string line)
 {
  return Lines(line) - m_mMark.Get(key);
 }

 protected void Step()
 {
  m_iT++;
  if (m_iT == 5)
  {
   bool spawned = Collect(m_A, m_aA) >= 6 && Collect(m_B, m_aB) >= 6;
   Check("spawned", spawned, string.Format("A %1, B %2", m_aA.Count(), m_aB.Count()));
   if (!spawned) { GetGame().GetCallqueue().Remove(Step); Finish(); return; }
   m_A.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
   BBB_Callouts.s_TestPlayer = m_B.GetLeaderEntity();
   m_iSaid0 = BBB_Callouts.s_iSaid;
   m_iHeld0 = BBB_Callouts.s_iPlayerHeld;
   m_iSaidByPlayer0 = BBB_Callouts.s_iSaidByPlayer;
  }
  if (m_iT == 8)
  {
   Mark("move", "ack"); Mark("move-leader", "order-move");
   Order(m_A, WP_MOVE, m_vFrom + m_vDir * 30);
   Order(m_B, WP_SUPPRESS, m_vFrom + m_vSide * 300 + m_vDir * 150);
  }
  if (m_iT == 20)
  {
   m_iOrderAnswers = Since("move", "ack") + Since("move-leader", "order-move");
   PrintFormat("[BBB] CALLOUTSTEST t=%1 move order: acks %2, leader's move %3", m_iT, Since("move", "ack"), Since("move-leader", "order-move"));
  }
  if (m_iT == 22) { Mark("suppress", "order-suppress"); Order(m_A, WP_SUPPRESS, m_vFrom + m_vDir * 150); }
  if (m_iT == 34) m_iOrderLeader = Since("suppress", "order-suppress");
  if (m_iT == 36)
  {
   Mark("cool-ack", "ack"); Mark("cool-move", "order-move"); Mark("cool-sup", "order-suppress"); Mark("cool-def", "order-defend");
  }
  if (m_iT == 36 || m_iT == 39 || m_iT == 42) Order(m_A, WP_MOVE, m_vFrom + m_vDir * (20 + (m_iT - 36) * 10)); // 30 m apart: each a new order
  if (m_iT == 47)
  {
   m_iCooldownAck = Since("cool-ack", "ack");
   m_iCooldownOrder = Since("cool-move", "order-move") + Since("cool-sup", "order-suppress") + Since("cool-def", "order-defend");
   PrintFormat("[BBB] CALLOUTSTEST t=%1 three orders 3 s apart: acks %2, leader's orders %3", m_iT, m_iCooldownAck, m_iCooldownOrder);
  }
  if (m_iT == 48)
  {
   Mark("contact", "contact"); Mark("contact-target", "contact-target"); Mark("contact-self", "contact-by-leader");
   m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vFrom + m_vDir * 150, m_vDir * -1));
  }
  if (m_iT == 52 && m_Enemy)
  {
   Collect(m_Enemy, m_aEnemy);
   m_Enemy.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
  }
  if (m_iT == 75)
  {
   m_iContact = Since("contact", "contact") + Since("contact-target", "contact-target") + Since("contact-self", "contact-by-leader");
   PrintFormat("[BBB] CALLOUTSTEST t=%1 contact calls %2 (leader's contact %3, leader's target call %4, leader spotted it himself %5)", m_iT, m_iContact, Since("contact", "contact"), Since("contact-target", "contact-target"), Since("contact-self", "contact-by-leader"));
  }
  if (m_iT == 76)
  {
   Mark("down", "man-down"); Mark("another", "another-man-down");
   Down(m_A, m_aA, 0);
   Down(m_B, m_aB, 0);
  }
  if (m_iT == 80) Down(m_A, m_aA, 0);
  if (m_iT < 96) return;
  GetGame().GetCallqueue().Remove(Step);
  Judge();
 }

 protected void Judge()
 {
  Check("order-answered", m_iOrderAnswers >= 1, string.Format("%1 (a man's roger or the leader's move)", m_iOrderAnswers));
  Check("order-suppress-voiced", m_iOrderLeader == 1, m_iOrderLeader.ToString());
  int said = BBB_Callouts.s_iSaid - m_iSaid0;
  Check("lines-transmitted", said >= 3, string.Format("%1 transmitted, %2 decided, %3 bypassed", said, BBB_Callouts.s_iDecided, BBB_Callouts.s_iBypassed));
  Check("order-cooldown", m_iCooldownAck <= 1 && m_iCooldownOrder <= 1 && m_iCooldownAck + m_iCooldownOrder >= 1, string.Format("acks %1, leader's orders %2 for three orders", m_iCooldownAck, m_iCooldownOrder));
  Check("contact-once", m_iContact == 1, string.Format("%1 lines in 27 s (cooldown %2 s)", m_iContact, 40 / BBB_Callouts.s_fRate));
  Check("man-down", Since("down", "man-down") >= 1, Since("down", "man-down").ToString());
  Check("another-man-down", Since("another", "another-man-down") >= 1, Since("another", "another-man-down").ToString());
  int bLines = BBB_Callouts.LinesOf(m_B);
  Check("player-squad-silent", bLines == 0, string.Format("%1 BBB lines for the player-led squad", bLines));
  int held = BBB_Callouts.s_iPlayerHeld - m_iHeld0;
  int bDown;
  foreach (IEntity man : m_aB)
   if (man && !BBB.IsAlive(man)) bDown++;
  Check("player-gate-asked", held >= 1 && bDown >= 1, string.Format("gate held %1 times, B men down %2", held, bDown));
  Check("no-player-voice", BBB_Callouts.s_iSaidByPlayer - m_iSaidByPlayer0 == 0, (BBB_Callouts.s_iSaidByPlayer - m_iSaidByPlayer0).ToString());
  foreach (string line, int count : BBB_Callouts.s_mDecided)
   PrintFormat("[BBB] CALLOUTSTEST line %1: decided %2, transmitted %3", line, count, BBB_Callouts.s_mSaid.Get(line));
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbcallouts passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_CalloutsTest.MaybeStartCallouts();
 }
}
