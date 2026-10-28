// Vanilla AI values from the Settings tab (BBB #8): skill and spotting speed for every AI
// soldier, set once when he spawns. Anything that sets them later (a host mod such as AC: Actual
// per squad, a Game Master, the Scenario Framework) still wins; BBB never sets them again.
// Default: untouched. Server only (AI runs there).
modded class SCR_AICombatComponent
{
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!Replication.IsServer()) return;
  string value;
  if (BBB_Settings.Get("bbbAISkill", value) && value != "0")
  {
   m_eAISkillDefault = value.ToInt();
   m_eAISkill = m_eAISkillDefault;
  }
  // The factor alone: vanilla applies it with the threat state's own on the next update.
  if (BBB_Settings.Get("bbbAIPerception", value)) m_fPerceptionFactor = Math.Clamp(value.ToFloat(), 0.25, 3);
 }
}
