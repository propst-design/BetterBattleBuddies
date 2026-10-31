// The game's own grenade-landing reaction reads the thrower's faction before it checks there is a
// thrower (SCR_AIDangerReaction_GrenadeLanding.PerformReaction), so a grenade whose thrower is gone
// (dead, or a grenade spawned by script) threw a null error and the soldier never got out of the way
// (#18). With no thrower he makes the same move away, with the same reaction delay; there is just
// nobody to report to the group. Every other grenade goes through the game's code unchanged.
// Built from the AI config, so it repeats vanilla's attribute (without it the class is unknown there).
[BaseContainerProps()]
modded class SCR_AIDangerReaction_GrenadeLanding
{
 override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
 {
  IEntity grenade = dangerEvent.GetObject();
  if (!grenade || GetShooterCharacter(grenade))
   return super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
  vector grenadePos = grenade.GetOrigin();
  float distance = vector.Distance(utility.GetOrigin(), grenadePos);
  if (distance >= GRENADE_AVOID_RADIUS)
   return false;
  float delay = GRENADE_AVOID_REACT_MIN_TIME_MS;
  if (distance > GRENADE_AVOID_RADIUS_MIN)
  {
   float visibility = GetGrenadeVisibilityFactor(utility.m_OwnerEntity, grenadePos);
   delay += GRENADE_AVOID_VIS_REACT_TIME_MS * (1 - visibility) + GetThreatLevelReactionTime(threatSystem) + GetDistanceSQReactionTime(distance);
  }
  utility.AddAction(new SCR_AIMoveFromGrenadeBehavior(utility, null, vector.Zero, grenade, delay, vector.Zero));
  return true;
 }
}