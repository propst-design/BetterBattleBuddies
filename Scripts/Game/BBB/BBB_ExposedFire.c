// Suppressed in the open (20 Oct 2026, Jacob): suppression should keep a man's head down
// when he has cover; without cover he has to fight back, faster and looser if anything.
// Vanilla treats both alike. THREATENED (suppression above 0.66) cuts his perception to 0.4
// (2.5 while alerted) and adds 0.8 s before the first shot, so a pinned man lying in the open
// stares at the enemy shooting him. For a THREATENED soldier on foot who is not in cover,
// BBB keeps the alerted perception, drops the extra delay and shortens his aiming pauses
// (fire-rate coefficient 1.6, unless an order slowed his fire). In cover, vanilla's
// hide-and-peek is unchanged. No new targets: he only fights what his own perception finds.
// Host option: -bbbExposedFire 0 (off).
class BBB_ExposedFire
{
	static int s_iRead;
	static bool s_bOn = true;
	static const float ALERTED_PERCEPTION = 2.5;	// vanilla PERCEPTION_FACTOR_ALERTED
	static const float OVERTHREATENED_WAIT_S = 0.8;	// vanilla WAIT_TIME_OVERTHREATENED
	static const float PANIC_FIRE_FACTOR = -0.6;	// SCR_AIGetFireTimes: 1 - fire-rate coefficient 1.6

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		string value;
		if (BBB_Settings.Get("bbbExposedFire", value)) s_bOn = value == "1";
		PrintFormat("[BBB] EXPOSED FIRE on=%1", s_bOn);
	}

	// Pinned (THREATENED), on foot, conscious, AI, and not in cover.
	static bool Exposed(SCR_AIUtilityComponent utility)
	{
		Read();
		if (!s_bOn || !utility || !utility.m_ThreatSystem || !utility.m_AIInfo || !utility.m_CombatMoveState) return false;
		if (utility.m_ThreatSystem.GetState() != EAIThreatState.THREATENED || utility.m_CombatMoveState.m_bInCover) return false;
		if (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) return false;
		return BBB.PlayerOf(utility.m_OwnerEntity) == 0;
	}
}

modded class SCR_AICombatComponent
{
	float m_fBBBExposedPerception; // what BBB set last update (0: vanilla's value stood); tests read it

	override void UpdatePerceptionFactor(PerceptionComponent perceptionComp, SCR_AIThreatSystem threatSystem)
	{
		super.UpdatePerceptionFactor(perceptionComp, threatSystem);
		m_fBBBExposedPerception = 0;
		if (!perceptionComp || !BBB_ExposedFire.Exposed(m_Utility)) return;
		m_fBBBExposedPerception = BBB_ExposedFire.ALERTED_PERCEPTION * m_fEquipmentPerceptionFactor * m_fPerceptionFactor;
		perceptionComp.SetPerceptionFactor(m_fBBBExposedPerception);
		BBB_Think.NoteUtility(m_Utility, "ExposedFire", "pinned in the open: keeps alert eyes, fires faster");
	}
}

modded class SCR_AIAttackBehavior
{
	override protected void InitWaitTime(SCR_AIUtilityComponent utility)
	{
		super.InitWaitTime(utility);
		if (!BBB_ExposedFire.Exposed(utility) || utility.m_ThreatSystem.GetThreatMeasure() < SCR_AIThreatSystem.THREATENED_THRESHOLD) return;
		m_fWaitTime.m_Value = Math.Max(0, m_fWaitTime.m_Value - BBB_ExposedFire.OVERTHREATENED_WAIT_S);
	}
}

modded class SCR_AIGetFireTimes
{
	protected SCR_AIUtilityComponent m_BBBUtility;

	override void OnInit(AIAgent owner)
	{
		super.OnInit(owner);
		m_BBBUtility = SCR_AIUtilityComponent.Cast(owner.FindComponent(SCR_AIUtilityComponent));
	}

	override float GetFireRateFactor()
	{
		float factor = super.GetFireRateFactor();
		// factor > 0: an order slowed his fire; it stands.
		if (factor > 0 || factor <= BBB_ExposedFire.PANIC_FIRE_FACTOR || !BBB_ExposedFire.Exposed(m_BBBUtility)) return factor;
		return BBB_ExposedFire.PANIC_FIRE_FACTOR;
	}
}
