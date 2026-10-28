// What an AI soldier is "thinking" (20 Oct 2026, Jacob). Each BBB feature that weighs a
// soldier or squad notes what it decided and why, including when it decided NOT to act
// ("skipped grenade: no confirmed enemy position"). The last few notes per soldier and per
// squad are kept in a small ring. With the overlay on, look at an AI soldier (the one
// nearest the centre of your view, up to 500 m) and a small card (BBB_ThinkOverlay) shows his
// vanilla behaviour, threat state, current target, whether a player order holds BBB back and
// his latest notes; a dot marks him and a red dot his target's last seen spot. The pick is
// sticky until you look at another soldier; the card hides while he is off screen.
// Only where the AI runs and someone looks: single player, Workbench or a listen host (on a
// dedicated server there is no camera; -bbbThink 2 still prints the notes to the log).
// Host option: -bbbThink 1 (notes + overlay), 2 (also "[BBB] THINK" log lines); off by default.
// Off, every note is one bool check.
class BBB_ThinkLine
{
 float m_fAt; // world time (ms) of the latest repeat
 string m_sFeature;
 string m_sText;
 int m_iRepeats = 1;
}

class BBB_ThinkLog
{
 ref array<ref BBB_ThinkLine> m_Lines = {};
 float m_fLast;
}

class BBB_Think
{
 static int s_iRead;
 static bool s_bOn;
 static bool s_bPrint;
 static const int LINES = 12;		// notes kept per soldier or squad
 static const int MAX_LOGS = 400;	// soldiers + squads tracked at once
 static const float FORGET_MS = 300000;	// a log nobody added to for 5 min is dropped
 static const float PICK_MS = 400;
 static const float PICK_RANGE_M = 500;
 static const float PICK_COS = 0.995;	// within ~5.7 degrees of the view centre
 protected static ref map<IEntity, ref BBB_ThinkLog> s_Logs = new map<IEntity, ref BBB_ThinkLog>();
 static IEntity s_Picked; // the soldier the overlay shows (tests read it)
 protected static float s_fNextPick;
 protected static float s_fNextPrune;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  if (BBB_Settings.Get("bbbThink", value))
  {
   s_bOn = value == "1" || value == "2";
   s_bPrint = value == "2";
  }
  PrintFormat("[BBB] THINK on=%1 print=%2", s_bOn, s_bPrint);
 }

 static void Stop()
 {
  s_Logs.Clear();
  s_Picked = null;
  BBB_ThinkOverlay.Remove();
  s_fNextPick = 0;
  s_fNextPrune = 0;
 }

 // ---------------------------------------------------------------- notes

 static void Note(IEntity who, string feature, string text)
 {
  if (!s_bOn || !who) return;
  float now = GetGame().GetWorld().GetWorldTime();
  BBB_ThinkLog entry = s_Logs.Get(who);
  if (!entry)
  {
   if (s_Logs.Count() >= MAX_LOGS)
   {
    Prune(now, true);
    // A busy battle may have no stale entries. Still enforce the memory bound.
    if (s_Logs.Count() >= MAX_LOGS)
    {
     IEntity oldest;
     float oldestAt = now + 1;
     foreach (IEntity candidate, BBB_ThinkLog existing : s_Logs)
     {
      if (existing.m_fLast < oldestAt)
      {
       oldest = candidate;
       oldestAt = existing.m_fLast;
      }
     }
     s_Logs.Remove(oldest);
    }
   }
   entry = new BBB_ThinkLog();
   s_Logs.Set(who, entry);
  }
  entry.m_fLast = now;
  int count = entry.m_Lines.Count();
  if (count > 0)
  {
   BBB_ThinkLine last = entry.m_Lines[count - 1];
   if (last.m_sFeature == feature && last.m_sText == text)
   {
    last.m_iRepeats++;
    last.m_fAt = now;
    return;
   }
  }
  BBB_ThinkLine line = new BBB_ThinkLine();
  line.m_fAt = now;
  line.m_sFeature = feature;
  line.m_sText = text;
  entry.m_Lines.Insert(line);
  if (entry.m_Lines.Count() > LINES) entry.m_Lines.RemoveOrdered(0);
  if (s_bPrint) PrintFormat("[BBB] THINK %1 %2: %3", Who(who), feature, text);
 }

 static void NoteAgent(AIAgent agent, string feature, string text)
 {
  if (!s_bOn || !agent) return;
  Note(agent.GetControlledEntity(), feature, text);
 }

 static void NoteUtility(SCR_AIUtilityComponent utility, string feature, string text)
 {
  if (!s_bOn || !utility) return;
  Note(utility.m_OwnerEntity, feature, text);
 }

 // Squad-wide decisions (shown under every member's panel).
 static void NoteGroup(AIGroup group, string feature, string text)
 {
  if (!s_bOn || !group) return;
  Note(group, feature, text);
 }

 static BBB_ThinkLog LogOf(IEntity who)
 {
  if (!who) return null;
  return s_Logs.Get(who);
 }

 static int LogCount() { return s_Logs.Count(); }

 // Logs whose soldier was deleted and not pruned yet (tests read it).
 static int NullLogs()
 {
  int count = 0;
  foreach (IEntity who, BBB_ThinkLog entry : s_Logs)
   if (!who) count++;
  return count;
 }

 static string Who(IEntity who)
 {
  SCR_AIGroup group = SCR_AIGroup.Cast(who);
  if (group) return BBB.s_Hooks.NameOf(group);
  return BBB_Medic.Name(who);
 }

 // Drops deleted soldiers and stale logs (idle 30 s when the store is full).
 static void Prune(float now, bool hard)
 {
  // Deleted entity keys become null; removing by that null key does not find the
  // original map slot. Rebuild from live keys instead of looking up dead ones.
  ref map<IEntity, ref BBB_ThinkLog> live = new map<IEntity, ref BBB_ThinkLog>();
  float cutoff = now - FORGET_MS;
  if (hard) cutoff = now - FORGET_MS / 10;
  foreach (IEntity who, BBB_ThinkLog entry : s_Logs)
   if (who && entry && entry.m_fLast >= cutoff) live.Set(who, entry);
  s_Logs = live;
 }

 static void Tick()
 {
  if (!s_bOn) return;
  float now = GetGame().GetWorld().GetWorldTime();
  if (now < s_fNextPrune) return;
  s_fNextPrune = now + 30000;
  Prune(now, false);
 }

 // ---------------------------------------------------------------- overlay

 static void Frame()
 {
  if (!s_bOn || !GetGame().GetPlayerController()) return;
  World world = GetGame().GetWorld();
  float now = world.GetWorldTime();
  if (now >= s_fNextPick)
  {
   s_fNextPick = now + PICK_MS;
   IEntity looked = Pick(world);
   if (looked) s_Picked = looked;
  }
  if (s_Picked) Draw(world, s_Picked, now);
  else BBB_ThinkOverlay.Hide();
 }

 // The AI soldier nearest the centre of the view (null: none close enough to it).
 static IEntity Pick(World world)
 {
  vector camera[4];
  world.GetCurrentCamera(camera);
  vector eye = camera[3];
  vector forward = camera[2];
  IEntity self = GetGame().GetPlayerController().GetControlledEntity();
  return PickFrom(eye, forward, self);
 }

 // Shared by the real camera and the headless direction/range acceptance test.
 static IEntity PickFrom(vector eye, vector forward, IEntity self)
 {
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<AIAgent> agents = {};
  IEntity best;
  float bestCos = PICK_COS;
  foreach (SCR_AIGroup group : groups)
  {
   BBB.s_Hooks.GetMembers(group, agents);
   foreach (AIAgent agent : agents)
   {
    if (!agent) continue;
    IEntity soldier = agent.GetControlledEntity();
    if (!soldier || soldier == self || BBB.PlayerOf(soldier)) continue;
    vector to = soldier.GetOrigin() + "0 1.2 0" - eye;
    float distance = to.Length();
    if (distance < 1 || distance > PICK_RANGE_M) continue;
    float cosine = vector.Dot(to * (1 / distance), forward);
    if (cosine <= bestCos) continue;
    bestCos = cosine;
    best = soldier;
   }
  }
  return best;
 }

 protected static string BehaviourName(SCR_AIBehaviorBase behaviour)
 {
  if (!behaviour) return "none";
  string name = behaviour.Type().ToString();
  name.Replace("SCR_AI", "");
  name.Replace("Behavior", "");
  return name;
 }

 protected static string Ago(float at, float now)
 {
  float seconds = (now - at) / 1000;
  return string.Format("%1s", Math.Round(seconds));
 }

 protected static void AddLines(BBB_ThinkLog entry, float now, notnull array<string> lines, int maxNotes = LINES)
 {
  if (!entry) return;
  for (int i = entry.m_Lines.Count() - 1; i >= 0 && i >= entry.m_Lines.Count() - maxNotes; i--)
  {
   BBB_ThinkLine line = entry.m_Lines[i];
   string repeats = "";
   if (line.m_iRepeats > 1) repeats = string.Format(" (x%1)", line.m_iRepeats);
   lines.Insert(string.Format("  -%1 %2: %3%4", Ago(line.m_fAt, now), line.m_sFeature, line.m_sText, repeats));
  }
 }

 // What the panel shows for one soldier (tests read it too).
 static void Describe(IEntity soldier, float now, notnull array<string> lines, out vector targetAt, int maxNotes = LINES)
 {
  lines.Clear();
  targetAt = vector.Zero;
  lines.Insert(BBB_Medic.Name(soldier));
  if (!BBB.IsAlive(soldier))
  {
   lines.Insert("dead or gone");
   AddLines(LogOf(soldier), now, lines, maxNotes);
   return;
  }
  AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
  AIAgent agent;
  if (control) agent = control.GetAIAgent();
  SCR_AIUtilityComponent utility;
  if (agent) utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (!utility)
  {
   lines.Insert("no AI brain (player or empty)");
   AddLines(LogOf(soldier), now, lines, maxNotes);
   return;
  }
  lines.Insert("doing: " + BehaviourName(utility.GetCurrentBehavior()));
  if (utility.m_ThreatSystem)
  {
   float threat = utility.m_ThreatSystem.GetThreatMeasure();
   lines.Insert(string.Format("threat: %1 (%2)", typename.EnumToString(EAIThreatState, utility.m_ThreatSystem.GetState()), threat.ToString(4, 2)));
  }
  string cover = "no";
  if (utility.m_CombatMoveState && utility.m_CombatMoveState.m_bInCover) cover = "yes";
  string order = "no";
  if (BBB_OrderIntent.Held(utility)) order = "YES (BBB stands back)";
  lines.Insert(string.Format("in cover: %1   player order: %2", cover, order));
  int role = BBB_Claims.Role(soldier);
  if (role != BBB_Claims.NONE) lines.Insert("BBB job: " + BBB_Claims.Name(role));
  BaseTarget target;
  if (utility.m_CombatComponent) target = utility.m_CombatComponent.GetCurrentTarget();
  if (target && target.GetTargetEntity())
  {
   targetAt = target.GetLastSeenPosition();
   lines.Insert(string.Format("target: %1, %2 m, %3, seen %4 s ago", BBB_Medic.Name(target.GetTargetEntity()), Math.Round(target.GetDistance()), typename.EnumToString(ETargetCategory, target.GetTargetCategory()), Math.Round(target.GetTimeSinceSeen())));
  }
  else lines.Insert("target: none");
  lines.Insert("this soldier:");
  AddLines(LogOf(soldier), now, lines, maxNotes);
  AIGroup group = agent.GetParentGroup();
  if (group)
  {
   lines.Insert("his squad (" + Who(group) + "):");
   string picture = BBB_Squad.Describe(SCR_AIGroup.Cast(group));
   if (picture != "") lines.Insert(picture);
   string tactic = BBB_Tactic.Describe(SCR_AIGroup.Cast(group));
   if (tactic != "") lines.Insert(tactic);
   AddLines(LogOf(group), now, lines, maxNotes);
  }
 }

 // The card's text changes at most every pick period; positions follow every frame.
 protected static float s_fNextText;
 protected static void Draw(World world, IEntity soldier, float now)
 {
  vector targetAt;
  if (now >= s_fNextText || !BBB_ThinkOverlay.HasText())
  {
   s_fNextText = now + PICK_MS;
   array<string> lines = {};
   Describe(soldier, now, lines, targetAt, BBB_ThinkOverlay.NOTES);
   BBB_ThinkOverlay.SetText(lines, targetAt);
  }
  BBB_ThinkOverlay.Place(world, soldier);
 }
}
