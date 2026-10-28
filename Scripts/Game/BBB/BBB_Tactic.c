// Squad tactic (squad brain stage 3, Jacob 24 Oct): one committed tactic per squad, chosen from
// its BBB_Squad picture, instead of every feature deciding for itself each tick. Short soldier
// reactions stay vanilla/BBB; the squad's intent holds (LAMBS-style commitments):
//   CONTACT   first contact (once per 10 min): the squad stops advancing for a few seconds while
//             vanilla calls the contact and the men turn to it
//   ASSAULT   85 s: fire and manoeuvre may run (BBB_FireManeuver admits only now); leader: "move"
//   HOLD      45 s: fight from where they are, no vanilla advances (m_bBBBTacticHold; Buildings
//             may still take a man indoors, vanilla still sends men to look at heard fire);
//             leader: "defend this position". Never inside 45 m
//             unless outnumbered: close in, the squad fights it out (ASSAULT)
//   FLANK     120 s (stage 4): instead of ASSAULT when the enemy has stayed put 20 s at 120-250 m
//             and a covered flank point exists (in that band the squad first holds and fires
//             until it knows he is staying put: fix, then flank); halves swap moving there and covering
//             (BBB_TacticDrill), then ASSAULT from the new angle; leader: "move" (to the point)
//   REORG     20 s after the leader goes down: hold, no new assault, then the acting leader picks
//   SEARCH    90 s (stage 4), once per contact: 20 s after contact is lost, halves take turns
//             looking at the last known position and the cover around it; leader: "search and
//             destroy", then "clear"
//   WITHDRAW  BBB_Morale's withdrawal outranks all; leader: "fall back"
// FLANK keeps a base of fire (the covering half suppresses the cover nearest the freshest real
// report, BBB_CoverFire; halves swap every 15 s). HOLD/REORG do the same only with
// -bbbTacticFire 1: in the A/B its bursts ended within a second (the men already had visible
// targets) and team-to-team noise swamped any effect.
// Only a conscious, unsuppressed leader picks; until then the tactic in force is extended. A
// commitment ends early only for withdrawal, leader loss, a player/GM order, contact gone 30 s,
// or an ASSAULT the squad can no longer run (host, order leash, fewer than four fit men) or that
// turns out to face twice its number.
// Host hook AllowSquadTactic(group); player squads, crews and player/GM orders are left alone.
//  -bbbTactic 0   off: features decide as before
//  -bbbTacticFlank 0 / -bbbTacticSearch 0   just that part off; -bbbTacticFire 1  HOLD base of fire on
class BBB_TacticState : Managed
{
 SCR_AIGroup m_Group;
 int m_iTactic;
 float m_fSince;         // world time s
 float m_fUntil;         // commitment ends
 float m_fNextLook;
 float m_fLastContact;
 IEntity m_Leader;       // last leader seen up
 int m_iMostSeen;        // most enemies seen in the main sector this fight...
 float m_fMostSeenAt;    // ...kept MOST_SEEN_S after it last rose (the engine forgets lost targets)
 int m_iAssaultMisses;   // consecutive looks an assault couldn't run (one glitch doesn't end it)
 // Stage 4 (BBB_TacticDrill)
 int m_iFireHalf;        // which half fires/covers now (the other moves or looks)
 float m_fSwapAt;
 vector m_vFlankPoint;
 vector m_vLastThreat;   // where the main threat was last seen
 vector m_vThreatSeen;   // ...and where it was when it last moved 15 m
 float m_fThreatStillSince;
 bool m_bSearched;       // one search per contact
 ref array<vector> m_aPoints = {};
 float m_fPointDeadline; // give up on the current search point after this
 ref array<ref BBB_AIWithdrawMove> m_aRuns = {};
 ref array<ref SCR_AIMoveAndInvestigateBehavior> m_aLooks = {};
 ref array<SCR_AIUtilityComponent> m_aLookers = {};
}

class BBB_Tactic
{
 static const int NONE = 0;
 static const int CONTACT = 1;
 static const int ASSAULT = 2;
 static const int HOLD = 3;
 static const int REORG = 4;
 static const int WITHDRAW = 5;
 static const int FLANK = 6;
 static const int SEARCH = 7;
 static const float FLANK_S = 120;
 static const float SEARCH_S = 90;
 static const float SEARCH_AFTER_S = 20;
 static const float FLANK_MIN_M = 120;
 static const float FLANK_MAX_M = 250;
 static const float STILL_S = 20;          // enemy stayed within 15 m this long: dug in, worth flanking
 static const float LOOK_S = 2;
 static const int STAGGER = 4;             // squads not yet in contact are checked every 4th tick
 static const float CONTACT_S = 5;
 static const float CONTACT_LOCK_S = 600;
 // How long a plan stands before it's judged again (-bbbTacticAssault/-Hold/-Reorg).
 static float s_fAssaultS = 85;
 static float s_fHoldS = 45;
 static float s_fReorgS = 20;
 static const float LOST_S = 30;
 static const float MOST_SEEN_S = 120;
 static const float LEADER_PINNED = 0.33;  // leader's native threat at or above this: no new pick
 static const float SQUAD_PINNED = 0.6;    // squad average at or above this: no assault
 static const float ASSAULT_MIN_M = 45;
 static const float ASSAULT_MAX_M = 300;
 static const float OUTNUMBERED = 1.5;     // enemies seen (last minute) in the main sector per conscious man: no assault
 static const float OVERRUN = 2;           // ...this many: an assault already running turns to HOLD
 static bool s_bOn = true;
 static bool s_bFlank = true;
 static bool s_bSearch = true;
 static bool s_bFire = false; // HOLD base of fire: opt-in until an A/B shows it adds fire (24 Oct)
 protected static int s_iRead;
 protected static ref map<SCR_AIGroup, ref BBB_TacticState> s_States = new map<SCR_AIGroup, ref BBB_TacticState>();
 static ref array<SCR_AIGroup> s_aVanilla = {}; // test only: squads left to the features' own rules
 static int s_iChanges; // tests read these
 protected static int s_iTick;
 static int s_iCallouts;
 // First contact plays once per squad per CONTACT_LOCK_S, across contacts (world time s).
 protected static ref map<SCR_AIGroup, float> s_mContactLock = new map<SCR_AIGroup, float>();

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbTactic", value) && value == "0");
  s_bFlank = !(BBB_Settings.Get("bbbTacticFlank", value) && value == "0");
  s_bSearch = !(BBB_Settings.Get("bbbTacticSearch", value) && value == "0");
  s_bFire = BBB_Settings.Get("bbbTacticFire", value) && value == "1";
  if (BBB_Settings.Get("bbbTacticAssault", value)) s_fAssaultS = Math.Clamp(value.ToFloat(), 30, 300);
  if (BBB_Settings.Get("bbbTacticHold", value)) s_fHoldS = Math.Clamp(value.ToFloat(), 15, 180);
  if (BBB_Settings.Get("bbbTacticReorg", value)) s_fReorgS = Math.Clamp(value.ToFloat(), 5, 60);
  PrintFormat("[BBB] SQUAD TACTIC enabled=%1 assault=%2s hold=%3s reorg=%4s flank=%5 search=%6 baseOfFire=%7", s_bOn, s_fAssaultS, s_fHoldS, s_fReorgS, s_bFlank, s_bSearch, s_bFire);
 }

 static string Name(int tactic)
 {
  if (tactic == CONTACT) return "contact";
  if (tactic == ASSAULT) return "assault";
  if (tactic == HOLD) return "hold";
  if (tactic == REORG) return "reorganising";
  if (tactic == WITHDRAW) return "withdraw";
  if (tactic == FLANK) return "flank";
  if (tactic == SEARCH) return "search";
  return "none";
 }

 protected static float Now() { return GetGame().GetWorld().GetWorldTime() / 1000; }

 // Is this squad run by a committed tactic?
 static bool Runs(SCR_AIGroup group)
 {
  return s_bOn && group && !s_aVanilla.Contains(group) && s_States.Contains(group);
 }

 static BBB_TacticState StateOf(SCR_AIGroup group)
 {
  if (!Runs(group)) return null;
  return s_States.Get(group);
 }

 static int TacticOf(SCR_AIGroup group)
 {
  if (!Runs(group)) return NONE;
  return s_States.Get(group).m_iTactic;
 }

 // May fire and manoeuvre run for this squad? (always, for squads the tactic doesn't run)
 static bool AllowsAssault(SCR_AIGroup group)
 {
  if (!Runs(group)) return true;
  int tactic = s_States.Get(group).m_iTactic;
  return tactic == ASSAULT || tactic == NONE;
 }

 static string Describe(SCR_AIGroup group)
 {
  if (!Runs(group)) return "";
  BBB_TacticState state = s_States.Get(group);
  if (state.m_iTactic == NONE) return "tactic: none";
  return string.Format("tactic: %1 (%2 s, %3 s left)", Name(state.m_iTactic), Math.Round(Now() - state.m_fSince), Math.Round(Math.Max(0, state.m_fUntil - Now())));
 }

 static void Stop()
 {
  foreach (SCR_AIGroup group, BBB_TacticState state : s_States)
   if (state) { BBB_TacticDrill.Clear(state); SetHold(state, false); }
  s_States.Clear();
  s_aVanilla.Clear();
  s_mContactLock.Clear();
 }

 // HOLD, REORG and the contact pause keep every member from vanilla advances.
 protected static void SetHold(BBB_TacticState state, bool hold)
 {
  if (!state.m_Group) return;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(state.m_Group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (utility) utility.m_bBBBTacticHold = hold;
  }
 }

 protected static bool Holds(int tactic)
 {
  return tactic == CONTACT || tactic == HOLD || tactic == REORG || tactic == FLANK || tactic == SEARCH;
 }

 protected static void Set(BBB_TacticState state, int tactic, float now, float commit, string why, vector threat)
 {
  if (state.m_iTactic == tactic && tactic != NONE) { state.m_fUntil = now + commit; return; }
  int old = state.m_iTactic;
  state.m_iTactic = tactic;
  state.m_fSince = now;
  state.m_fUntil = now + commit;
  BBB_TacticDrill.Clear(state);
  state.m_iFireHalf = 0;
  state.m_fSwapAt = now + BBB_TacticDrill.SWAP_S;
  SetHold(state, Holds(tactic));
  s_iChanges++;
  PrintFormat("[BBB] TACTIC %1: %2 -> %3 (%4)", BBB.s_Hooks.NameOf(state.m_Group), Name(old), Name(tactic), why);
  BBB_Think.NoteGroup(state.m_Group, "Tactic", Name(tactic) + ": " + why);
  CallOut(state.m_Group, tactic, threat);
 }

 // The leader (or the next conscious man) says the order, through vanilla's comms queue.
 protected static void CallOut(SCR_AIGroup group, int tactic, vector threat)
 {
  ECommunicationType type = ECommunicationType.NONE;
  if ((tactic == ASSAULT || tactic == FLANK) && threat.LengthSq() > 0) type = ECommunicationType.REPORT_MOVE;
  else if (tactic == SEARCH) type = ECommunicationType.REPORT_SEARCH_AND_DESTROY;
  else if (tactic == HOLD) type = ECommunicationType.REPORT_DEFEND;
  else if (tactic == WITHDRAW) type = ECommunicationType.REPORT_RETURN;
  Say(group, type, threat);
 }

 protected static void Say(SCR_AIGroup group, ECommunicationType type, vector threat)
 {
  if (type == ECommunicationType.NONE) return;
  SCR_AIUtilityComponent speaker = Speaker(group);
  if (!speaker || !speaker.m_CommsHandler || speaker.m_CommsHandler.CanBypass()) return;
  speaker.m_CommsHandler.AddRequest(new SCR_AITalkRequest(type, null, threat, 0, true, false, SCR_EAITalkRequestPreset.MANDATORY));
  s_iCallouts++;
 }

 protected static SCR_AIUtilityComponent Speaker(SCR_AIGroup group)
 {
  IEntity leader = group.GetLeaderEntity();
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  SCR_AIUtilityComponent first;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man || !BBB.IsAlive(man) || man.GetCharacterController().IsUnconscious()) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (!utility) continue;
   if (man == leader) return utility;
   if (!first) first = utility;
  }
  return first;
 }

 protected static void Drop(SCR_AIGroup group)
 {
  BBB_TacticState state = s_States.Get(group);
  if (state) { BBB_TacticDrill.Clear(state); SetHold(state, false); }
  s_States.Remove(group);
 }

 static void Tick()
 {
  Read();
  if (!s_bOn) { if (!s_States.IsEmpty()) Stop(); return; }
  float now = Now();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  for (int i = s_States.Count() - 1; i >= 0; i--)
  {
   SCR_AIGroup known = s_States.GetKey(i);
   if (!known || !groups.Contains(known))
   {
    BBB_TacticState gone = s_States.GetElement(i);
    if (gone) { BBB_TacticDrill.Clear(gone); SetHold(gone, false); }
    s_States.RemoveElement(i);
   }
  }
  for (int c = s_mContactLock.Count() - 1; c >= 0; c--)
   if (!s_mContactLock.GetKey(c) || now >= s_mContactLock.GetElement(c)) s_mContactLock.RemoveElement(c);
  for (int v = s_aVanilla.Count() - 1; v >= 0; v--)
   if (!s_aVanilla[v]) s_aVanilla.Remove(v);
  s_iTick++;
  foreach (int index, SCR_AIGroup group : groups)
  {
   if (!group) continue;
   BBB_TacticState state = s_States.Get(group);
   // Squads not in contact are looked at a quarter at a time (host checks included).
   if (!state && (index + s_iTick) % STAGGER != 0) continue;
   if (s_aVanilla.Contains(group) || !BBB.s_Hooks.AllowSquadTactic(group) || BBB.HasPlayer(group) || BBB_Transport.CrewedVehicle(group)) { if (s_States.Contains(group)) Drop(group); continue; }
   if (!state)
   {
    // Only squads in contact get a state.
    if (!BBB_Squad.On(group)) continue;
    vector seen;
    if (!BBB_Squad.Threat(group, BBB_Squad.RECENT_S, seen)) continue;
    state = new BBB_TacticState();
    state.m_Group = group;
    state.m_fLastContact = now;
    state.m_fThreatStillSince = now;
    state.m_Leader = group.GetLeaderEntity();
    s_States.Set(group, state);
   }
   if (now < state.m_fNextLook) continue;
   state.m_fNextLook = now + LOOK_S;
   Look(state, now);
  }
 }

 protected static void Look(BBB_TacticState state, float now)
 {
  SCR_AIGroup group = state.m_Group;
  // A player or GM order on anyone: the squad is theirs.
  if (OrderHeld(group)) { Drop(group); return; }
  if (BBB_Morale.IsWithdrawing(group)) { Set(state, WITHDRAW, now, LOOK_S * 2, "morale broke", vector.Zero); return; }
  vector threat;
  bool flanked;
  int enemies;
  bool contact = BBB_Squad.MainSector(group, BBB_Squad.RECENT_S, threat, flanked, enemies);
  // The odds count everyone seen this fight (the most at once, for 2 min): men out of sight are still there.
  vector remembered;
  bool wide;
  int known;
  if (contact && BBB_Squad.MainSector(group, BBB_Squad.MEMORY_S, remembered, wide, known)) enemies = Math.Max(enemies, known);
  if (enemies > state.m_iMostSeen || now - state.m_fMostSeenAt > MOST_SEEN_S) { state.m_iMostSeen = enemies; state.m_fMostSeenAt = now; }
  enemies = Math.Max(enemies, state.m_iMostSeen);
  if (contact)
  {
   state.m_fLastContact = now;
   state.m_vLastThreat = threat;
   if (vector.DistanceXZ(threat, state.m_vThreatSeen) > 15) { state.m_vThreatSeen = threat; state.m_fThreatStillSince = now; }
  }
  BBB_SquadPicture picture = BBB_Squad.Get(group);
  if (!picture) return;
  if (!contact)
  {
   if (state.m_iTactic == SEARCH)
   {
    if (BBB_TacticDrill.Search(state, now) || now >= state.m_fUntil) { Say(group, ECommunicationType.REPORT_CLEAR, vector.Zero); PrintFormat("[BBB] TACTIC %1: search over", BBB.s_Hooks.NameOf(group)); Drop(group); }
    return;
   }
   float lost = now - state.m_fLastContact;
   if (s_bSearch && lost >= SEARCH_AFTER_S && !state.m_bSearched && state.m_iTactic != WITHDRAW && picture.m_iConscious >= 2 && state.m_vLastThreat.LengthSq() > 0)
   {
    state.m_bSearched = true;
    BBB_TacticDrill.SearchPoints(state, state.m_vLastThreat, picture.m_vCentre, state.m_aPoints);
    if (!state.m_aPoints.IsEmpty())
    {
     Set(state, SEARCH, now, SEARCH_S, string.Format("contact lost %1 s: %2 places to look", Math.Round(lost), state.m_aPoints.Count()), vector.Zero);
     state.m_fPointDeadline = now + BBB_TacticDrill.POINT_S + vector.DistanceXZ(picture.m_vCentre, state.m_aPoints[0]) / 1.5;
     BBB_TacticDrill.Search(state, now);
     return;
    }
   }
   if (lost > LOST_S) { Drop(group); return; }
  }
  else if (state.m_iTactic == SEARCH) state.m_fUntil = now; // found again: pick afresh
  // Leader down: the acting leader takes over only after the squad reorganises.
  IEntity leader = group.GetLeaderEntity();
  if (state.m_Leader && state.m_Leader != leader && Down(state.m_Leader))
  {
   state.m_Leader = leader;
   Set(state, REORG, now, s_fReorgS, "leader down", vector.Zero);
   return;
  }
  if (leader && !Down(leader)) state.m_Leader = leader;
  bool assaultFits = contact && CanAssault(group, picture, threat, flanked, enemies);
  // Hysteresis breakers: an assault the squad can no longer run.
  if (state.m_iTactic == ASSAULT && now < state.m_fUntil && !assaultFits && !BaseAssault(group, picture)) state.m_iAssaultMisses++;
  else state.m_iAssaultMisses = 0;
  if (state.m_iAssaultMisses >= 2) { state.m_iAssaultMisses = 0; Set(state, HOLD, now, s_fHoldS, "can't keep assaulting", vector.Zero); return; }
  if ((state.m_iTactic == ASSAULT || state.m_iTactic == FLANK) && now < state.m_fUntil && enemies >= picture.m_iConscious * OVERRUN) { Set(state, HOLD, now, s_fHoldS, string.Format("more enemies than thought (%1 seen, %2 of us)", enemies, picture.m_iConscious), vector.Zero); return; }
  if (state.m_iTactic == WITHDRAW) state.m_iTactic = NONE; // withdrawal over: pick afresh
  // Carry out the tactic in force (stage 4).
  if (state.m_iTactic == FLANK)
  {
   if (BBB_TacticDrill.Flank(state, now)) { Set(state, ASSAULT, now, s_fAssaultS, "in position on the flank", threat); return; }
  }
  else if (s_bFire && (state.m_iTactic == HOLD || state.m_iTactic == REORG))
  {
   array<SCR_AIUtilityComponent> members = {};
   BBB_TacticDrill.Members(state, members);
   BBB_TacticDrill.Swap(state, now);
   BBB_TacticDrill.BaseOfFire(state, now, members);
  }
  if (now < state.m_fUntil) return;
  if (!contact) return; // nothing to decide on; the tactic runs out at LOST_S
  if (state.m_iTactic == NONE && (!s_mContactLock.Contains(group) || now >= s_mContactLock.Get(group)))
  {
   s_mContactLock.Set(group, now + CONTACT_LOCK_S);
   Set(state, CONTACT, now, CONTACT_S, "first contact", vector.Zero);
   return;
  }
  // Only a conscious leader who isn't pinned down picks; otherwise the squad keeps its tactic.
  SCR_AIUtilityComponent deciding = LeaderUtility(group);
  if (!deciding || (deciding.m_ThreatSystem && deciding.m_ThreatSystem.GetThreatMeasure() >= LEADER_PINNED))
  {
   if (state.m_iTactic == CONTACT || state.m_iTactic == NONE || state.m_iTactic == REORG) Set(state, HOLD, now, s_fHoldS, "leader pinned down", vector.Zero);
   else state.m_fUntil = now + LOOK_S;
   return;
  }
  if (assaultFits)
  {
   // A dug-in enemy at 120-250 m is worth going round rather than at.
   float range = vector.DistanceXZ(threat, picture.m_vCentre);
   vector flankAt;
   // In flanking range, first fix him: hold and fire until he has shown he is staying put.
   float still = now - state.m_fThreatStillSince;
   if (s_bFlank && range >= FLANK_MIN_M && range <= FLANK_MAX_M && still < STILL_S && BaseAssault(group, picture) && state.m_iTactic != HOLD)
   {
    Set(state, HOLD, now, STILL_S - still + 1, string.Format("fixing the enemy %1 m away before deciding", Math.Round(range)), vector.Zero);
    return;
   }
   if (s_bFlank && range >= FLANK_MIN_M && range <= FLANK_MAX_M && now - state.m_fThreatStillSince >= STILL_S && BaseAssault(group, picture) && BBB_TacticDrill.FlankPoint(state, picture, threat, flankAt))
   {
    state.m_vFlankPoint = flankAt;
    Set(state, FLANK, now, FLANK_S, string.Format("enemy dug in %1 m away: flanking to a point %2 m from us", Math.Round(range), Math.Round(vector.DistanceXZ(flankAt, picture.m_vCentre))), flankAt);
    return;
   }
   Set(state, ASSAULT, now, s_fAssaultS, string.Format("enemy %1 m, %2 seen, %3 of us", Math.Round(range), enemies, picture.m_iConscious), threat);
  }
  else Set(state, HOLD, now, s_fHoldS, HoldReason(group, picture, threat, flanked, enemies), vector.Zero);
 }

 // What an assault needs regardless of where the enemy is: the host and orders allow fire and
 // manoeuvre, and four fit men.
 protected static bool BaseAssault(SCR_AIGroup group, BBB_SquadPicture picture)
 {
  return BBB_FireManeuver.s_bOn && picture.m_iConscious >= 4 && BBB_FireManeuver.GroupAllowed(group);
 }

 protected static bool CanAssault(SCR_AIGroup group, BBB_SquadPicture picture, vector threat, bool flanked, int enemies)
 {
  float range = vector.DistanceXZ(threat, picture.m_vCentre);
  if (enemies >= picture.m_iConscious * OUTNUMBERED) return false;
  // Close in, the squad fights it out (vanilla close combat; fire and manoeuvre keeps its own
  // 45 m floor): no hold for being pinned, flanked or few.
  if (range < ASSAULT_MIN_M) return true;
  if (flanked || range > ASSAULT_MAX_M || picture.m_fAvgThreat >= SQUAD_PINNED) return false;
  return BaseAssault(group, picture);
 }

 protected static string HoldReason(SCR_AIGroup group, BBB_SquadPicture picture, vector threat, bool flanked, int enemies)
 {
  float range = vector.DistanceXZ(threat, picture.m_vCentre);
  if (flanked) return "flanked";
  if (enemies >= picture.m_iConscious * OUTNUMBERED) return string.Format("outnumbered (%1 seen, %2 of us)", enemies, picture.m_iConscious);
  if (picture.m_fAvgThreat >= SQUAD_PINNED) return "pinned down";
  if (range > ASSAULT_MAX_M) return string.Format("enemy too far (%1 m)", Math.Round(range));
  if (picture.m_iConscious < 4) return "too few to bound";
  return "orders or host keep them in place";
 }

 protected static bool Down(IEntity soldier)
 {
  if (!soldier || !BBB.IsAlive(soldier)) return true;
  ChimeraCharacter character = ChimeraCharacter.Cast(soldier);
  return character && character.GetCharacterController() && character.GetCharacterController().IsUnconscious();
 }

 protected static SCR_AIUtilityComponent LeaderUtility(SCR_AIGroup group)
 {
  IEntity leader = group.GetLeaderEntity();
  if (Down(leader)) return null;
  AIControlComponent control = AIControlComponent.Cast(leader.FindComponent(AIControlComponent));
  if (!control || !control.GetAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 protected static bool OrderHeld(SCR_AIGroup group)
 {
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   if (BBB_OrderIntent.Held(SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent)))) return true;
  }
  return false;
 }

 static int Count() { return s_States.Count(); }
}

modded class SCR_AIUtilityComponent
{
 bool m_bBBBTacticHold; // squad tactic HOLD / REORG / contact pause: fight from here, don't advance
}
