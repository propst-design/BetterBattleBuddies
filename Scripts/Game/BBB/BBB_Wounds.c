// Which hurts are combat wounds (Jacob, 6 Nov 2026). A soldier snagged on barbed wire, hurt in a
// fall or a crash, or burnt at a fire is not under attack: his squad emptied their smoke grenades
// screening a man caught on the wire in front of a fortification. BBB's alarm reactions to a
// casualty (medic smoke, contact fire toward where the damage came from) wait for a real combat
// wound: a bullet, a fragment, a blast, an incendiary, or a blow from another man.
// The hit is recorded on the server by BBB's character damage hook (BBB_Armour.c), on the
// soldier's own damage manager (no map to prune). Bleeding, healing and regeneration ticks
// aren't combat and never refresh it.
class BBB_Wounds
{
 // A combat wound older than this no longer counts: by then he is treated, dead or long
 // past the fight that hurt him.
 static const float COMBAT_WOUND_S = 600;

 static bool IsCombat(BaseDamageContext context, IEntity victim)
 {
  if (!context) return false;
  EDamageType type = context.damageType;
  if (type == EDamageType.KINETIC || type == EDamageType.EXPLOSIVE || type == EDamageType.FRAGMENTATION) return true;
  if (type == EDamageType.PROCESSED_FRAGMENTATION || type == EDamageType.INCENDIARY) return true;
  // A rifle butt or a bayonet counts; a melee hurt with nobody behind it (wire, a prop) doesn't.
  if (type != EDamageType.MELEE || !context.instigator) return false;
  IEntity by = context.instigator.GetInstigatorEntity();
  return by && by != victim && ChimeraCharacter.Cast(by);
 }

 static void Record(SCR_CharacterDamageManagerComponent damage, BaseDamageContext context)
 {
  if (!damage || !GetGame() || !GetGame().GetWorld()) return;
  if (!IsCombat(context, damage.GetOwner())) return;
  damage.m_fBBBCombatHitMs = GetGame().GetWorld().GetWorldTime();
 }

 // Seconds since his last combat wound, -1 if he has none on record.
 static float SinceCombatHit(IEntity soldier)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(soldier);
  if (!character) return -1;
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
  if (!damage || damage.m_fBBBCombatHitMs < 0) return -1;
  return (GetGame().GetWorld().GetWorldTime() - damage.m_fBBBCombatHitMs) / 1000.0;
 }

 // Was he wounded in combat lately (within COMBAT_WOUND_S)?
 static bool CombatWound(IEntity soldier)
 {
  float since = SinceCombatHit(soldier);
  return since >= 0 && since <= COMBAT_WOUND_S;
 }

 // Was he hit in combat in the last few seconds?
 static bool HitJustNow(IEntity soldier, float withinS)
 {
  float since = SinceCombatHit(soldier);
  return since >= 0 && since <= withinS;
 }
}
