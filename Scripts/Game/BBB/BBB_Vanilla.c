// Vanilla adapter (5 Oct 2026, plan step 2): BBB runs in any scenario (Conflict, Game
// Master, scenario framework missions) without AC: Actual. 10 s after the game starts, if
// no host mod has started BBB, it starts itself with BBB_VanillaHooks (every AI group in
// the world) and drives BBB.Frame from the call queue every frame. A host mod that starts
// later (BBB.Start with its own hooks) simply takes over the hooks; BBB.Frame runs once per
// frame whoever calls it.
// What works without AC: Actual: everything that hooks the game's AI (aim fixes, gunner
// drill, small arms vs armour, drag/carry with ACE, medic smoke deployment, safer healing
// if switched on, bios), plus the ticked helpers over all groups: gates opened ahead of AI
// vehicles, soldiers set down from truck beds, vehicle doors closed, tactical cargo dismount
// under fire, speed-aware stalled vehicle unstick (BBB_Unstick), and spent launchers dropped.
// Host option: -bbbVanilla 0 keeps BBB from starting itself.
class BBB_VanillaHooks : BBB_Hooks
{
 protected ref array<AIAgent> m_Agents = {};
 protected ref array<SCR_AIGroup> m_Cache = {};
 protected float m_fCachedAt = -1;

 // Every AI group with at least one AI member (refreshed at most once a second).
 override void GetGroups(notnull array<SCR_AIGroup> groups)
 {
  groups.Clear();
  float now = GetGame().GetWorld().GetWorldTime();
  if (now - m_fCachedAt >= 1000 || m_fCachedAt < 0)
  {
   m_fCachedAt = now;
   m_Cache.Clear();
   AIWorld world = GetGame().GetAIWorld();
   if (world)
   {
    m_Agents.Clear();
    world.GetAIAgents(m_Agents);
    foreach (AIAgent agent : m_Agents)
    {
     SCR_AIGroup group = SCR_AIGroup.Cast(agent);
     if (group && group.GetAgentsCount() > 0) m_Cache.Insert(group);
    }
   }
  }
  foreach (SCR_AIGroup cached : m_Cache)
   if (cached) groups.Insert(cached);
 }

 override string NameOf(SCR_AIGroup group)
 {
  if (!group) return "group";
  string name = group.GetCustomName();
  if (name.IsEmpty())
  {
   string company, platoon, squad, character, format;
   group.GetCallsigns(company, platoon, squad, character, format);
   if (!format.IsEmpty()) name = WidgetManager.Translate(format, company, platoon, squad, character);
  }
  if (name.IsEmpty()) name = "group " + group.GetGroupID();
  return name;
 }
}

class BBB_Vanilla
{
 static bool s_bFrameSource;

 static void OnGameStart()
 {
  if (!Replication.IsServer()) return;
  string value;
  if (BBB_Settings.Get("bbbVanilla", value) && value == "0") return;
  GetGame().GetCallqueue().CallLater(StartIfAlone, 10000);
 }

 static void StartIfAlone()
 {
  if (!s_bFrameSource)
  {
   s_bFrameSource = true;
   GetGame().GetCallqueue().CallLater(BBB.Frame, 0, true);
  }
  if (BBB.IsStarted()) return;
  BBB.Start(new BBB_VanillaHooks());
  Print("[BBB] started on its own (vanilla adapter: all AI groups)");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_Vanilla.OnGameStart();
 }
}
