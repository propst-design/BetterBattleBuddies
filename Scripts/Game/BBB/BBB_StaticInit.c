// Builds BBB's static collections (job lists, caches, scratch arrays) when the game object is created.
// The engine runs every class-level static initializer ("static ref array<T> s_X = {};",
// "= new map<...>()") from ONE hidden function with an instruction cap shared by all loaded
// mods. Over the cap the "Game" module fails to compile ("Too many instructions per function"
// on random local consts), so BBB declares its statics bare and fills them here instead.
// tools/check_static_init.py (CI) keeps new initializers out.
class BBB_StaticInit : Managed
{
 protected static bool s_bDone;

 static void Run()
 {
  if (s_bDone) return;
  s_bDone = true;
  BBB.BBB_InitStatics();
  BBB_Aim.BBB_InitStatics();
  BBB_ArmorReaction.BBB_InitStatics();
  BBB_Armour.BBB_InitStatics();
  BBB_Bios.BBB_InitStatics();
  BBB_Buildings.BBB_InitStatics();
  BBB_Claims.BBB_InitStatics();
  BBB_ContactRelay.BBB_InitStatics();
  BBB_CoverFire.BBB_InitStatics();
  BBB_CrewStays.BBB_InitStatics();
  BBB_CrewLeader.BBB_InitStatics();
  BBB_SquadLeader.BBB_InitStatics();
  BBB_CrewTurnout.BBB_InitStatics();
  BBB_DoorUnstick.BBB_InitStatics();
  BBB_Drag.BBB_InitStatics();
  BBB_DriverAlignment.BBB_InitStatics();
  BBB_FireManeuver.BBB_InitStatics();
  BBB_Flares.BBB_InitStatics();
  BBB_Grenades.BBB_InitStatics();
  BBB_Gunner.BBB_InitStatics();
  BBB_HeldGuns.BBB_InitStatics();
  BBB_HorizonScan.BBB_InitStatics();
  BBB_LauncherUse.BBB_InitStatics();
  BBB_Medic.BBB_InitStatics();
  BBB_Morale.BBB_InitStatics();
  BBB_OrderLeash.BBB_InitStatics();
  BBB_Perf.BBB_InitStatics();
  BBB_Rearm.BBB_InitStatics();
  BBB_RelaxedMarch.BBB_InitStatics();
  BBB_Settings.BBB_InitStatics();
  BBB_Smoke.BBB_InitStatics();
  BBB_Squad.BBB_InitStatics();
  BBB_StaticWeapons.BBB_InitStatics();
  BBB_Tactic.BBB_InitStatics();
  BBB_TacticDrill.BBB_InitStatics();
  BBB_Teams.BBB_InitStatics();
  BBB_Think.BBB_InitStatics();
  BBB_ThinkOverlay.BBB_InitStatics();
  BBB_Threat.BBB_InitStatics();
  BBB_Transport.BBB_InitStatics();
  BBB_Unstick.BBB_InitStatics();
  BBB_VehicleHelpers.BBB_InitStatics();
  // Test classes (Scripts/Game/BBB_Tests, left out of the release) fill theirs through this hook.
  BBB_StaticInit tests = new BBB_StaticInit();
  tests.InitTestStatics();
 }

 // Overridden by BBB_Tests/BBB_TestStaticInit.c; does nothing in the release.
 protected void InitTestStatics() {}
}
