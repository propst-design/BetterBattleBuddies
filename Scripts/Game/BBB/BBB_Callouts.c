// Squad leader callouts (12 Nov 2026, Claude Code). An AI squad leader voices what the squad is doing
// and what he wants from his men, in the game's own radio-protocol voice (no text, no new sounds):
// a new order, BBB's tactics (fire and manoeuvre, flank, hold, fall back, search), contact, a man down,
// forming up after a fight. A man answers an order with vanilla's "roger".
// How it sounds: every line goes through the speaker's own SCR_AICommsHandler queue, so vanilla's channel
// rule (nobody talks over a nearby friend), LOD bypass, faction voice and the VoN radio/3D path apply.
// Lines vanilla AI has an ECommunicationType for go as plain SCR_AITalkRequests; radio-protocol lines it
// has no type for (man down, follow me, suppress, fall back, roger: the ones the player's own commanding
// menu plays) ride a request carrying the sound event (modded SCR_AITalkRequest.m_sBBBEvent), played by
// the modded SCR_AICommsHandler.TransmitRequest on the same VoN component with vanilla's signals.
// Who speaks: only the group's leader, and only when he is AI (never a player's mouth: a player-led squad
// gets nothing from here; vanilla already voices the player's own commands). Answers: an AI man near him.
// How often: per squad a gap between BBB lines and a cooldown per kind; a line that has to wait (the squad
// is talking) waits up to 8 s. Nobody talks over a nearby friend (vanilla's channel rule). No idle talk at all.
// Who else talks: vanilla (group leader on Move/Defend/Search waypoints, the spotter's contact report with
// direction, "covering"/"moving" from bounding men, "clear"), BBB_Tactic/BBB_SquadLeader/BBB_CrewLeader.
// The tactic and split lines now pass through here; the crew leader's target calls (vehicle commanders)
// don't, but count as the squad having spoken.
//  -bbbCallouts 0       off (the tactic callouts BBB had before go too)
//  -bbbCalloutRate 1    how chatty: 0.25 (rare) .. 3 (busy); divides every cooldown and gap
//  -bbbCalloutLog 1     log every line decided (tests turn it on)
class BBB_CalloutPending : Managed
{
 string m_sLine;
 ECommunicationType m_eType;
 string m_sEvent;
 IEntity m_Entity;
 vector m_vPos;
 int m_iSignal;
 int m_iKind;
 float m_fNotBefore;
 float m_fExpires;
 bool m_bAck; // a man answers once it is said
}

class BBB_CalloutState : Managed
{
 SCR_AIGroup m_Group;
 bool m_bSeen;
 AIWaypoint m_Waypoint;
 typename m_tOrder;      // the last order voiced or answered: its kind and place
 vector m_vOrder;
 float m_fLastLine = -1000;
 float m_fLastSpoke = -1000;
 float m_fLeaderOrderAt = -1000; // the leader transmitted an order line (vanilla's move/defend/search/mount or ours)
 ref array<float> m_aKindAt = {};
 ref array<IEntity> m_aUp = {};
 float m_fDownSaidAt = -1000; // a man-down line was said
 bool m_bInContact;
 float m_fContactLeft = -1;
 ref array<ref BBB_CalloutPending> m_aPending = {};
}

class BBB_Callouts
{
 // Kinds, each with its own cooldown (seconds at rate 1).
 static const int ORDER = 0;
 static const int CONTACT = 1;
 static const int DOWN = 2;
 static const int REGROUP = 3;
 static const int TACTIC = 4;
 static const int SUPPRESS = 5;
 static const int KINDS = 6;
 static const float GAP_S = 5;           // between two BBB lines of one squad
 static const float ORDER_DELAY_S = 2;   // let vanilla's own waypoint call go first
 static const float ACK_DELAY_MS = 2600; // the answer after the order (vanilla samples are 2 s)
 static const float ACK_M = 40;
 static const float REGROUP_QUIET_S = 20;
 static const float REGROUP_SPREAD_M = 30;
 static const float ANOTHER_DOWN_S = 40;
 static const float PENDING_S = 8;
 static const float SAME_ORDER_M = 25;  // a host re-issuing the same order there isn't a new one

 static bool s_bOn;
 static float s_fRate;
 static bool s_bLog;
 protected static int s_iRead;
 protected static ref map<SCR_AIGroup, ref BBB_CalloutState> s_States;
 protected static ref array<float> s_aCooldown;
 // Counters (tests): lines decided (queued or bypassed), lines actually transmitted, by line id.
 static int s_iDecided;
 static int s_iSaid;
 static int s_iBypassed;
 static int s_iPlayerHeld;
 static int s_iThrottled;
 static ref map<string, int> s_mDecided;
 static ref map<string, int> s_mSaid;
 static ref map<SCR_AIGroup, int> s_mByGroup; // lines decided per squad
 static ref map<string, int> s_mGroupLine;     // "<group> <line>" decided (tests)
 static int s_iSaidByPlayer;                  // BBB lines transmitted by a player (must stay 0)
 // Tests only: a man treated as player-controlled (a hidden server has no players).
 static IEntity s_TestPlayer;

 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_States = new map<SCR_AIGroup, ref BBB_CalloutState>();
  s_mDecided = new map<string, int>();
  s_mSaid = new map<string, int>();
  s_mByGroup = new map<SCR_AIGroup, int>();
  s_mGroupLine = new map<string, int>();
  s_aCooldown = {12, 40, 8, 90, 15, 25};
  s_bOn = true;
  s_fRate = 1;
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbCallouts", value) && value == "0");
  if (BBB_Settings.Get("bbbCalloutRate", value)) s_fRate = Math.Clamp(value.ToFloat(), 0.25, 3);
  s_bLog = BBB_Settings.Get("bbbCalloutLog", value) && value == "1";
  PrintFormat("[BBB] CALLOUTS enabled=%1 rate=%2 log=%3", s_bOn, s_fRate, s_bLog);
 }

 static void Stop()
 {
  if (s_States) s_States.Clear();
  if (s_mByGroup) s_mByGroup.Clear();
  s_TestPlayer = null;
 }

 static bool On()
 {
  Read();
  return s_bOn;
 }

 protected static float Now()
 {
  BaseWorld world = GetGame().GetWorld();
  if (!world) return 0;
  return world.GetWorldTime() * 0.001;
 }

 protected static BBB_CalloutState State(SCR_AIGroup group)
 {
  if (!group || !s_States) return null;
  BBB_CalloutState state = s_States.Get(group);
  if (state) return state;
  state = new BBB_CalloutState();
  state.m_Group = group;
  for (int i = 0; i < KINDS; i++) state.m_aKindAt.Insert(-1000);
  s_States.Set(group, state);
  return state;
 }

 static bool IsPlayer(IEntity man)
 {
  if (!man) return false;
  if (man == s_TestPlayer) return true;
  ChimeraCharacter character = ChimeraCharacter.Cast(man);
  if (!character || !character.GetCharacterController()) return false;
  return character.GetCharacterController().IsPlayerControlled();
 }

 // An AI man able to talk: alive, awake, not a player.
 protected static bool CanTalk(IEntity man)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(man);
  if (!character || !BBB.IsAlive(character) || character.GetCharacterController().IsUnconscious()) return false;
  return !IsPlayer(character);
 }

 protected static SCR_AICommsHandler CommsOf(IEntity man)
 {
  if (!man) return null;
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
  if (!utility) return null;
  return utility.m_CommsHandler;
 }

 // The squad leader who may speak for this group, or null (player leader, down, a zombie's group,
 // a host that keeps the group quiet).
 static IEntity Leader(SCR_AIGroup group)
 {
  if (!group) return null;
  IEntity leader = group.GetLeaderEntity();
  if (!leader) return null;
  if (IsPlayer(leader)) { s_iPlayerHeld++; return null; }
  if (!CanTalk(leader) || BBB_Zombies.IsZombie(leader)) return null;
  if (!BBB.s_Hooks.AllowCallouts(group)) return null;
  return leader;
 }

 // ---- the gate every line passes ----

 // Queue a line from the squad leader. type NONE with an event: a radio-protocol line vanilla AI has
 // no type for. False when it isn't said (off, no AI leader, throttled).
 static bool Say(SCR_AIGroup group, int kind, string line, ECommunicationType type, string sound, IEntity entity, vector pos, int signal)
 {
  if (!On() || !group) return false;
  BBB_CalloutState state = State(group);
  if (!state) return false;
  IEntity leader = Leader(group);
  if (!leader) return false;
  float now = Now();
  // Talking over a friend is vanilla's channel rule (the request waits in his queue); here only how often.
  // A man down doesn't wait for the gap (it has its own cooldown).
  bool gap = kind != DOWN && now - state.m_fLastLine < GAP_S / s_fRate;
  if (gap || now - state.m_aKindAt[kind] < s_aCooldown[kind] / s_fRate)
  {
   s_iThrottled++;
   if (s_bLog) PrintFormat("[BBB] CALLOUT %1: %2 held (%3 s since the last line, %4 s since the last of its kind)", BBB.s_Hooks.NameOf(group), line, (now - state.m_fLastLine).ToString(4, 1), (now - state.m_aKindAt[kind]).ToString(4, 1));
   return false;
  }
  SCR_AICommsHandler comms = CommsOf(leader);
  if (!comms || comms.GetMuted()) return false;
  state.m_fLastLine = now;
  state.m_aKindAt[kind] = now;
  Queue(group, leader, comms, line, type, sound, entity, pos, signal, SCR_EAITalkRequestPreset.MEDIUM);
  return true;
 }

 // Through the man's own comms queue. A bypassed request (far from every player, LOD) still counts as
 // decided: the squad behaves the same whether anyone is there to hear it.
 protected static void Queue(SCR_AIGroup group, IEntity speaker, SCR_AICommsHandler comms, string line, ECommunicationType type, string sound, IEntity entity, vector pos, int signal, SCR_EAITalkRequestPreset preset)
 {
  s_iDecided++;
  s_mDecided.Set(line, s_mDecided.Get(line) + 1);
  s_mByGroup.Set(group, s_mByGroup.Get(group) + 1);
  Note(group, line);
  bool bypass = comms.CanBypass();
  if (s_bLog) PrintFormat("[BBB] CALLOUT %1: %2 by %3%4", BBB.s_Hooks.NameOf(group), line, speaker, Bypassed(bypass));
  if (bypass) { s_iBypassed++; return; }
  SCR_AITalkRequest request = new SCR_AITalkRequest(type, entity, pos, signal, true, false, preset);
  request.m_sBBBEvent = sound;
  request.m_sBBBLine = line;
  request.m_BBBGroup = group;
  comms.AddRequest(request);
 }

 static string GroupLine(SCR_AIGroup group, string line)
 {
  return string.Format("%1 %2", group, line);
 }

 protected static void Note(SCR_AIGroup group, string line)
 {
  string key = GroupLine(group, line);
  s_mGroupLine.Set(key, s_mGroupLine.Get(key) + 1);
 }

 protected static string Bypassed(bool bypass)
 {
  if (bypass) return " (bypassed: nobody near enough to hear)";
  return "";
 }

 // A line is going out (modded SCR_AICommsHandler, any request, vanilla's too): the squad has spoken.
 static void OnTransmit(AIAgent agent, SCR_AITalkRequest request)
 {
  if (!agent || !s_States) return;
  SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
  if (!group) return;
  BBB_CalloutState state = s_States.Get(group);
  if (!state) return;
  state.m_fLastSpoke = Now();
  if (request && agent.GetControlledEntity() == group.GetLeaderEntity() && OrderLine(request)) state.m_fLeaderOrderAt = Now();
  if (!request || request.m_sBBBLine.IsEmpty()) return;
  s_iSaid++;
  if (IsPlayer(agent.GetControlledEntity())) s_iSaidByPlayer++;
  s_mSaid.Set(request.m_sBBBLine, s_mSaid.Get(request.m_sBBBLine) + 1);
  if (s_bLog) PrintFormat("[BBB] CALLOUT SAID %1: %2 by %3", BBB.s_Hooks.NameOf(group), request.m_sBBBLine, agent.GetControlledEntity());
 }

 protected static bool OrderLine(SCR_AITalkRequest request)
 {
  if (request.m_sBBBLine.IndexOf("order-") == 0) return true;
  ECommunicationType type = request.m_eCommType;
  return type == ECommunicationType.REPORT_MOVE || type == ECommunicationType.REPORT_DEFEND || type == ECommunicationType.REPORT_SEARCH_AND_DESTROY || type == ECommunicationType.REPORT_MOUNT || type == ECommunicationType.REPORT_MOUNT_NEAREST || type == ECommunicationType.REPORT_UNMOUNT;
 }

 // Try again on the next ticks if the squad is busy talking (an important line shouldn't be lost to a
 // vanilla "moving" call), until it goes stale.
 protected static void Later(SCR_AIGroup group, int kind, string line, ECommunicationType type, string sound, IEntity entity, vector pos, int signal, float delay, bool ack = false)
 {
  BBB_CalloutState state = State(group);
  if (!state) return;
  float now = Now();
  for (int i = state.m_aPending.Count() - 1; i >= 0; i--)
   if (state.m_aPending[i].m_iKind == kind) state.m_aPending.Remove(i);
  BBB_CalloutPending pending = new BBB_CalloutPending();
  pending.m_sLine = line;
  pending.m_eType = type;
  pending.m_sEvent = sound;
  pending.m_Entity = entity;
  pending.m_vPos = pos;
  pending.m_iSignal = signal;
  pending.m_iKind = kind;
  pending.m_fNotBefore = now + delay;
  pending.m_fExpires = now + delay + PENDING_S;
  pending.m_bAck = ack;
  state.m_aPending.Insert(pending);
 }

 // An AI man near the leader answers ("roger"), after the leader's line.
 protected static void Acknowledge(SCR_AIGroup group)
 {
  GetGame().GetCallqueue().CallLater(AcknowledgeNow, ACK_DELAY_MS, false, group);
 }

 protected static void AcknowledgeNow(SCR_AIGroup group)
 {
  if (!On() || !group) return;
  IEntity leader = Leader(group);
  if (!leader) return;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  IEntity best;
  float bestM = ACK_M;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   IEntity man = agent.GetControlledEntity();
   if (!man || man == leader || !CanTalk(man)) continue;
   float m = vector.Distance(man.GetOrigin(), leader.GetOrigin());
   if (m >= bestM) continue;
   bestM = m;
   best = man;
  }
  SCR_AICommsHandler comms = CommsOf(best);
  if (!comms || comms.GetMuted()) return;
  Queue(group, best, comms, "ack", ECommunicationType.NONE, "SOUND_CP_POSITIVEFEEDBACK", null, vector.Zero, 0, SCR_EAITalkRequestPreset.IRRELEVANT);
 }

 // ---- events ----

 // A new order: vanilla's leader already says Move/Defend/Search on the activity; when he did, a man
 // answers; when he didn't (follow, suppress, a waypoint vanilla is silent on, a player's or GM's order
 // given while the squad talked), the leader says it, then a man answers.
 protected static void Order(BBB_CalloutState state, float changedAt)
 {
  SCR_AIGroup group = state.m_Group;
  AIWaypoint waypoint = state.m_Waypoint;
  if (!waypoint || group.GetCurrentWaypoint() != waypoint) return;
  // The same kind of order to (about) the same place again: a host refreshing it, not a new order.
  vector at = waypoint.GetOrigin();
  if (waypoint.Type() == state.m_tOrder && vector.DistanceXZ(at, state.m_vOrder) < SAME_ORDER_M) return;
  state.m_tOrder = waypoint.Type();
  state.m_vOrder = at;
  if (state.m_fLeaderOrderAt >= changedAt)
  {
   if (s_bLog) PrintFormat("[BBB] CALLOUT %1: the leader gave the order himself (%2)", BBB.s_Hooks.NameOf(group), waypoint.Type());
   if (Leader(group) && Now() - state.m_aKindAt[ORDER] >= s_aCooldown[ORDER] / s_fRate)
   {
    state.m_aKindAt[ORDER] = Now();
    Acknowledge(group);
   }
   return;
  }
  if (s_bLog) PrintFormat("[BBB] CALLOUT %1: new order %2, the leader says it", BBB.s_Hooks.NameOf(group), waypoint.Type());
  // Waits for a quiet moment (up to PENDING_S) if the squad is talking; a newer order replaces it.
  if (SCR_SuppressWaypoint.Cast(waypoint)) Later(group, ORDER, "order-suppress", ECommunicationType.NONE, "SOUND_CP_COMBAT_ORDER_SUPPRESS", null, vector.Zero, 0, 0, true);
  else if (SCR_DefendWaypoint.Cast(waypoint)) Later(group, ORDER, "order-defend", ECommunicationType.REPORT_DEFEND, "", null, vector.Zero, 0, 0, true);
  else if (SCR_SearchAndDestroyWaypoint.Cast(waypoint)) Later(group, ORDER, "order-search", ECommunicationType.REPORT_SEARCH_AND_DESTROY, "", null, vector.Zero, 0, 0, true);
  else if (SCR_EntityWaypoint.Cast(waypoint)) Later(group, ORDER, "order-follow", ECommunicationType.NONE, "SOUND_CP_FOLLOW_LEADER", null, vector.Zero, 0, 0, true);
  else if (SCR_BoardingWaypoint.Cast(waypoint)) Later(group, ORDER, "order-mount", ECommunicationType.REPORT_MOUNT_NEAREST, "", null, vector.Zero, 0, 0, true);
  else Later(group, ORDER, "order-move", ECommunicationType.REPORT_MOVE, "", null, waypoint.GetOrigin(), 0, 0, true);
 }

 // Vanilla's group perception filtered a new enemy (BBB_Combat's modded SCR_AIGroupUtilityComponent).
 // The spotter calls it himself (vanilla, with direction and distance); the leader follows with a
 // target call. When the spotter can't be heard calling (a player, or bypassed), the leader calls the
 // contact with direction himself. Zombies too: contact yes (suppression lines come only from BBB's
 // fire and manoeuvre, which never runs on zombies).
 static void OnContact(SCR_AIGroup group, SCR_AITargetInfo target, AIAgent reporter)
 {
  if (!On() || !group || !target || !target.m_Entity) return;
  BBB_CalloutState state = State(group);
  if (!state) return;
  state.m_bInContact = true;
  state.m_fContactLeft = -1;
  IEntity leader = Leader(group);
  if (!leader) return;
  if (Now() - state.m_aKindAt[CONTACT] < s_aCooldown[CONTACT] / s_fRate) return;
  IEntity spotter;
  if (reporter) spotter = reporter.GetControlledEntity();
  if (spotter == leader)
  {
   // He saw it first: vanilla has him call it, with direction and distance. Counted, nothing added.
   state.m_aKindAt[CONTACT] = Now();
   s_mDecided.Set("contact-by-leader", s_mDecided.Get("contact-by-leader") + 1);
   Note(group, "contact-by-leader");
   if (s_bLog) PrintFormat("[BBB] CALLOUT %1: the leader spotted %2 himself (vanilla's contact call)", BBB.s_Hooks.NameOf(group), target.m_Entity);
   return;
  }
  SCR_AICommsHandler spotterComms = CommsOf(spotter);
  bool heard = spotter && !IsPlayer(spotter) && spotterComms && !spotterComms.CanBypass();
  if (heard) Later(group, CONTACT, "contact-target", ECommunicationType.REPORT_TARGET, "", target.m_Entity, vector.Zero, 0, 2.5);
  else Later(group, CONTACT, "contact", ECommunicationType.REPORT_CONTACT, "", target.m_Entity, target.m_vWorldPos, 0, 0);
 }

 // BBB_Tactic changed the squad's plan.
 static bool Tactic(SCR_AIGroup group, string line, ECommunicationType type, string sound, IEntity entity, vector pos, int signal)
 {
  return Say(group, TACTIC, line, type, sound, entity, pos, signal);
 }

 // Fire and manoeuvre starts: the covering half suppresses while the other bounds.
 static void Suppress(SCR_AIGroup group, vector threat)
 {
  Say(group, SUPPRESS, "suppress", ECommunicationType.NONE, "SOUND_CP_COMBAT_ORDER_SUPPRESS", null, threat, 0);
 }

 // A squad falls back (morale break or the host's covered fall-back).
 static bool FallBack(SCR_AIGroup group)
 {
  return Say(group, TACTIC, "fall-back", ECommunicationType.NONE, "SOUND_CP_MOVEMENT_ORDER_RETREAT", null, vector.Zero, 0);
 }

 // The flank: "<callsign>, flank left/right" to a man of the moving half (vanilla's REPORT_FLANK: its
 // sample index 0 is right, 1 left). Without a callsign the leader says the move instead.
 static void Flank(SCR_AIGroup group, IEntity runner, vector from, vector threat, vector point)
 {
  vector ahead = threat - from;
  vector toPoint = point - from;
  float cross = ahead[0] * toPoint[2] - ahead[2] * toPoint[0];
  int side = 1;
  if (cross < 0) side = 0; // clockwise of the line to the enemy: his right
  if (runner && runner.FindComponent(SCR_CallsignCharacterComponent))
   Say(group, TACTIC, "flank", ECommunicationType.REPORT_FLANK, "", runner, vector.Zero, side);
  else
   Say(group, TACTIC, "flank-move", ECommunicationType.REPORT_MOVE, "", null, point, 0);
 }

 // ---- once a second (BBB.Tick) ----

 static void Tick()
 {
  if (!On() || !s_States) return;
  float now = Now();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  foreach (SCR_AIGroup group : groups)
  {
   if (!group) continue;
   BBB_CalloutState state = State(group);
   Watch(state, now);
  }
  // Forget groups that are gone.
  array<SCR_AIGroup> gone = {};
  foreach (SCR_AIGroup known, BBB_CalloutState kept : s_States)
   if (!known) gone.Insert(known);
  foreach (SCR_AIGroup dead : gone) s_States.Remove(dead);
 }

 protected static void Watch(BBB_CalloutState state, float now)
 {
  SCR_AIGroup group = state.m_Group;
  // New order: the current waypoint changed (not on the first look: a squad spawned with orders).
  AIWaypoint waypoint = group.GetCurrentWaypoint();
  if (waypoint != state.m_Waypoint)
  {
   state.m_Waypoint = waypoint;
   if (waypoint && state.m_bSeen) GetGame().GetCallqueue().CallLater(OrderLater, ORDER_DELAY_S * 1000, false, group, waypoint, now);
  }
  // Men down: a man up on the last look who is now dead or unconscious (one who left alive isn't).
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  int down;
  foreach (IEntity was : state.m_aUp)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(was);
   if (!man) continue; // deleted
   if (!BBB.IsAlive(man) || man.GetCharacterController().IsUnconscious()) down++;
  }
  state.m_aUp.Clear();
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter member = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && BBB.IsAlive(member) && !member.GetCharacterController().IsUnconscious()) state.m_aUp.Insert(member);
  }
  // A man-down line still waiting covers the next man too; "another" only after one was said.
  if (down > 0 && state.m_bSeen && !Waiting(state, DOWN))
  {
   string line = "man-down";
   string sound = "SOUND_CP_REPORTS_MANDOWN";
   if (now - state.m_fDownSaidAt < ANOTHER_DOWN_S) { line = "another-man-down"; sound = "SOUND_CP_REPORTS_ANOTHERMANDOWN"; }
   Later(group, DOWN, line, ECommunicationType.NONE, sound, null, vector.Zero, 0, 0);
  }
  // Out of contact a while after a fight, squad spread out: "on me".
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  bool contact = utility && utility.m_Perception && utility.m_Perception.m_MostDangerousCluster;
  if (contact) { state.m_bInContact = true; state.m_fContactLeft = -1; }
  else if (state.m_bInContact)
  {
   if (state.m_fContactLeft < 0) state.m_fContactLeft = now;
   if (now - state.m_fContactLeft >= REGROUP_QUIET_S)
   {
    state.m_bInContact = false;
    if (Spread(group, agents) > REGROUP_SPREAD_M) Say(group, REGROUP, "regroup", ECommunicationType.NONE, "SOUND_CP_FOLLOW_LEADER", null, vector.Zero, 0);
   }
  }
  state.m_bSeen = true;
  // Lines waiting for a quiet moment.
  for (int i = state.m_aPending.Count() - 1; i >= 0; i--)
  {
   BBB_CalloutPending pending = state.m_aPending[i];
   if (now > pending.m_fExpires) { state.m_aPending.Remove(i); continue; }
   if (now < pending.m_fNotBefore) continue;
   if (pending.m_Entity == null && (pending.m_eType == ECommunicationType.REPORT_TARGET || pending.m_eType == ECommunicationType.REPORT_CONTACT)) { state.m_aPending.Remove(i); continue; }
   if (!Say(group, pending.m_iKind, pending.m_sLine, pending.m_eType, pending.m_sEvent, pending.m_Entity, pending.m_vPos, pending.m_iSignal)) continue;
   if (pending.m_bAck) Acknowledge(group);
   if (pending.m_iKind == DOWN) state.m_fDownSaidAt = now;
   state.m_aPending.Remove(i);
  }
 }

 protected static bool Waiting(BBB_CalloutState state, int kind)
 {
  foreach (BBB_CalloutPending pending : state.m_aPending)
   if (pending.m_iKind == kind) return true;
  return false;
 }

 protected static void OrderLater(SCR_AIGroup group, AIWaypoint waypoint, float changedAt)
 {
  if (!group || !s_States) return;
  BBB_CalloutState state = s_States.Get(group);
  if (!state || state.m_Waypoint != waypoint) return;
  Order(state, changedAt);
 }

 // How far the farthest awake man is from the leader.
 protected static float Spread(SCR_AIGroup group, array<AIAgent> agents)
 {
  IEntity leader = group.GetLeaderEntity();
  if (!leader) return 0;
  float far = 0;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   IEntity man = agent.GetControlledEntity();
   if (!man || !BBB.IsAlive(man)) continue;
   far = Math.Max(far, vector.DistanceXZ(man.GetOrigin(), leader.GetOrigin()));
  }
  return far;
 }
}

// The line a BBB request carries: a radio-protocol sound event when vanilla AI has no type for it, and
// the id the log and tests read.
modded class SCR_AITalkRequest
{
 string m_sBBBEvent;
 string m_sBBBLine;
 SCR_AIGroup m_BBBGroup;
}

modded class SCR_AICommsHandler
{
 override protected void TransmitRequest(SCR_AITalkRequest rq)
 {
  BBB_Callouts.OnTransmit(m_Agent, rq);
  if (!rq || rq.m_sBBBEvent.IsEmpty())
  {
   super.TransmitRequest(rq);
   return;
  }
  // As SCR_AISoundHandling.SetSignalsAndTransmit does for its own lines, and as the player's commanding
  // menu plays these events: a random variant, addressed to everyone.
  rq.m_eState = SCR_EAITalkRequestState.TRANSMITTING;
  if (!m_VoNComponent || !m_SignalsManagerComponent) return;
  array<float> signals = {};
  SCR_AISoundHandling.SetSignal(m_SignalsManagerComponent.FindSignal("Seed"), Math.RandomFloat(0, 1), signals);
  SCR_AISoundHandling.SetSignal_SoldierCalledValues(m_SignalsManagerComponent, SCR_AISoundHandling.SIGNAL_VALUE_SOLDIER_ALL, signals);
  m_VoNComponent.SoundEventPriority(rq.m_sBBBEvent, signals, SCR_AISoundHandling.NORMAL_PRIORITY);
 }
}
