// Backing a stuck vehicle out: the game's AI driver often sits against a wall,
// a tree or another vehicle with the throttle down. The host decides when a
// vehicle is stuck (and what to do after); this does the manoeuvre: the driver's
// AI is parked, the vehicle reverses with the wheel turned for a few seconds and
// brakes, then the driver gets his AI back. A vehicle still rolling forward (an
// unarmed one backing away from an armed enemy ahead, BBB_Traffic) brakes to a
// stop first: the gearbox refuses reverse at speed, and the reverse throttle
// then drove it on in its forward gear (srv:mannersevade, 4 Nov: a truck at
// 60 km/h sped up to 62 and passed 13 m from the BTR it was backing away from). Inputs are written every frame
// (BBB.Frame): the vehicle controller drops inputs set less often.
// The driver's horn is released too: an AI driver honking at a soldier in its way
// keeps the horn pressed forever if its AI is parked mid-honk.

// Told when a manoeuvre is over (finished, driver lost or timed out).
class BBB_UnstickListener : Managed
{
 void OnUnstickEnded(IEntity vehicle) {}
}

class BBB_Unstick : Managed
{
 static const float REVERSE_MS = 3500;    // reversing, then a short brake
 static const float TOTAL_MS = 4300;
 static const float ROLLING_SPEED = 1.5;     // m/s forward: brake to a stop before reversing
 static const float BRAKE_MAX_MS = 6000;  // at most this long braking first

 protected static ref array<ref BBB_Unstick> s_Active;
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Active = {};
 }

 IEntity m_Vehicle;
 ChimeraCharacter m_Driver;
 protected float m_fStart;
 protected float m_fSteer;
 protected bool m_bBraking = true;   // still rolling forward: stop first (cleared once slow or after BRAKE_MAX_MS)
 protected bool m_bParkedAI; // only hand back an AI this parked (something else may have parked it)
 protected BaseVehicleControllerComponent m_PilotHeld; // driver seat unhooked while we drive (BBB_PilotHold)
 protected ref BBB_UnstickListener m_Listener;

 // Start backing this vehicle out; steer is the wheel while reversing (-1..1).
 static void Begin(IEntity vehicle, ChimeraCharacter driver, float steer, BBB_UnstickListener listener)
 {
  BBB_Unstick unstick = new BBB_Unstick();
  unstick.m_Vehicle = vehicle;
  unstick.m_Driver = driver;
  unstick.m_fStart = GetGame().GetWorld().GetWorldTime();
  unstick.m_fSteer = steer;
  unstick.m_Listener = listener;
  AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
  if (ai && ai.IsAIActivated())
  {
   ai.DeactivateAI();
   unstick.m_bParkedAI = true;
  }
  ReleaseHorn(driver);
  s_Active.Insert(unstick);
 }

 // Throttle, wheel and brake for a parked driver's vehicle (written every frame: the vehicle controller
 // drops inputs set less often).
 static void Drive(ChimeraCharacter driver, VehicleWheeledSimulation sim, float throttle, float steer, float brake)
 {
  sim.SetThrottle(throttle);
  sim.SetSteering(steer);
  sim.SetBreak(brake, false);
 }

 // Let go of the horn (an AI parked mid-honk never releases it itself).
 static void ReleaseHorn(ChimeraCharacter character)
 {
  if (!character) return;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller) return;
  CharacterInputContext input = controller.GetInputContext();
  if (input && input.GetVehicleHorn() != 0) input.SetVehicleHorn(0);
 }

 // Drop a manoeuvre without telling its owner (it is gone). Inputs are still released and a
 // parked driver gets his AI back: dropping it bare left him frozen in his seat for good.
 static void Cancel(BBB_UnstickListener listener)
 {
  for (int i = s_Active.Count() - 1; i >= 0; i--)
  {
   BBB_Unstick unstick = s_Active[i];
   if (unstick.m_Listener != listener) continue;
   unstick.m_Listener = null;
   CarControllerComponent car;
   if (unstick.m_Vehicle) car = CarControllerComponent.Cast(unstick.m_Vehicle.FindComponent(CarControllerComponent));
   unstick.End(car);
   s_Active.RemoveOrdered(i);
  }
 }

 // BBB is stopping: finish every manoeuvre (inputs released, parked AI handed back), tell no one.
 static void StopAll()
 {
  foreach (BBB_Unstick unstick : s_Active)
  {
   unstick.m_Listener = null;
   CarControllerComponent car;
   if (unstick.m_Vehicle) car = CarControllerComponent.Cast(unstick.m_Vehicle.FindComponent(CarControllerComponent));
   unstick.End(car);
  }
  s_Active.Clear();
 }

 static void FrameAll()
 {
  for (int i = s_Active.Count() - 1; i >= 0; i--)
   if (!s_Active[i].Frame()) s_Active.RemoveOrdered(i);
 }

 // Returns false when finished.
 protected bool Frame()
 {
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_fStart;
  IEntity vehicle = m_Vehicle;
  // A player who takes the driver over (a Game Master possessing him) or a driver who changed
  // seats ends it: inputs written every frame would steer the car under the player.
  bool alive = vehicle != null && m_Driver != null && BBB.IsAlive(m_Driver) && BBB_Transport.VehicleOf(m_Driver) == vehicle && !BBB.PlayerOf(m_Driver) && BBB.DriverOf(vehicle) == m_Driver;
  // The car even when it ends that way: End lets go of the throttle, wheel and reverse gear once,
  // or a player taking the wheel would find it still backing out.
  CarControllerComponent car;
  if (vehicle) car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
  if (!alive || !car || !car.GetSimulation() || (!m_bBraking && elapsed > TOTAL_MS))
  {
   End(car);
   return false;
  }
  VehicleWheeledSimulation sim = car.GetSimulation();
  Physics physics = vehicle.GetPhysics();
  if (physics && !physics.IsActive()) physics.SetActive(ActiveState.ACTIVE);
  if (!car.IsEngineOn()) car.ForceStartEngine();
  if (!m_PilotHeld && BBB_PilotHold.Take(car)) m_PilotHeld = car;
  car.SetPersistentHandBrake(false);
  ReleaseHorn(m_Driver);
  int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
  sim.SetClutch(1);
  // Rolling forward: brake to a stop first; the reverse's time starts once it is slow.
  if (m_bBraking)
  {
   float forward;
   if (physics) forward = vector.Dot(physics.GetVelocity(), vehicle.GetTransformAxis(2));
   if (forward > ROLLING_SPEED && elapsed < BRAKE_MAX_MS)
   {
    Drive(m_Driver, sim, 0, 0, 1);
    return true;
   }
   m_bBraking = false;
   m_fStart = GetGame().GetWorld().GetWorldTime();
   elapsed = 0;
  }
  if (elapsed <= REVERSE_MS)
  {
   if (sim.GetGear() >= neutral) sim.SetGear(neutral - 1);
   Drive(m_Driver, sim, 0.7, m_fSteer, 0);
  }
  else
  {
   Drive(m_Driver, sim, 0, 0, 1);
  }
  return true;
 }

 protected void End(CarControllerComponent car)
 {
  if (car && car.GetSimulation())
  {
   VehicleWheeledSimulation sim = car.GetSimulation();
   Drive(m_Driver, sim, 0, 0, 0);
   int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
   sim.SetGear(neutral + 1);
  }
  if (m_PilotHeld) BBB_PilotHold.Give(m_PilotHeld);
  m_PilotHeld = null;
  ReleaseHorn(m_Driver);
  if (m_bParkedAI && m_Driver && BBB.IsAlive(m_Driver) && !BBB.PlayerOf(m_Driver))
  {
   AIControlComponent ai = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
   if (ai && !ai.IsAIActivated()) ai.ActivateAI();
  }
  if (m_Listener) m_Listener.OnUnstickEnded(m_Vehicle);
 }
}
