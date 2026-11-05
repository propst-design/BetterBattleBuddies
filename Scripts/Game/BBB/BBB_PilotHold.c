// While BBB drives a parked AI driver's vehicle itself (armour reverse, unstick, firing-arc
// alignment, pull-over), the driver seat is unhooked from the vehicle's controller and handed back
// when the manoeuvre ends. From Reforger 1.9 the controller feeds the seat's input into the
// simulation every physics step and its automatic gearbox picks the gear from that input; a parked
// driver's input is empty, so BBB's gear and throttle were overwritten (a truck told to reverse sat
// in neutral or crept forward). ACA's gunner-drive unhooks the seat the same way. Each holder
// takes once and gives once; the seat goes back when the last holder gives it.
class BBB_PilotHoldEntry : Managed
{
 PilotCompartmentSlot m_Seat;
 int m_iHolders;
}

class BBB_PilotHold
{
 protected static ref map<BaseVehicleControllerComponent, ref BBB_PilotHoldEntry> s_Held;

 // Unhook the driver seat; true if the caller now holds it (and must Give it back).
 static bool Take(BaseVehicleControllerComponent controller)
 {
  if (!controller) return false;
  if (!s_Held) s_Held = new map<BaseVehicleControllerComponent, ref BBB_PilotHoldEntry>();
  Purge();
  BBB_PilotHoldEntry entry = s_Held.Get(controller);
  if (entry)
  {
   entry.m_iHolders++;
   return true;
  }
  // Already unhooked by someone else (a host mod's own driving): not ours to take.
  PilotCompartmentSlot seat = controller.GetPilotCompartmentSlot();
  if (!seat) return false;
  controller.SetPilotCompartmentSlot(null);
  entry = new BBB_PilotHoldEntry();
  entry.m_Seat = seat;
  entry.m_iHolders = 1;
  s_Held.Insert(controller, entry);
  return true;
 }

 static void Give(BaseVehicleControllerComponent controller)
 {
  if (!s_Held || !controller) return;
  BBB_PilotHoldEntry entry = s_Held.Get(controller);
  if (!entry) return;
  entry.m_iHolders--;
  if (entry.m_iHolders > 0) return;
  controller.SetPilotCompartmentSlot(entry.m_Seat);
  s_Held.Remove(controller);
 }

 // The driver seat, held or not: BBB's "who drives this" must not lose the driver mid-manoeuvre.
 static PilotCompartmentSlot Seat(BaseVehicleControllerComponent controller)
 {
  if (!controller) return null;
  if (s_Held)
  {
   BBB_PilotHoldEntry entry = s_Held.Get(controller);
   if (entry) return entry.m_Seat;
  }
  return controller.GetPilotCompartmentSlot();
 }

 static bool Held(BaseVehicleControllerComponent controller)
 {
  return controller && s_Held && s_Held.Contains(controller);
 }

 // BBB is driving this vehicle itself right now (braking, reversing, turning it): a host's own
 // "stopped, so stuck" watch should wait until the seat is given back.
 static bool Driving(IEntity vehicle)
 {
  if (!vehicle || !s_Held) return false;
  return Held(BaseVehicleControllerComponent.Cast(vehicle.FindComponent(BaseVehicleControllerComponent)));
 }

 // BBB stopping: every seat goes back, whoever held it.
 static void GiveAll()
 {
  if (!s_Held) return;
  for (int i = s_Held.Count() - 1; i >= 0; i--)
  {
   BaseVehicleControllerComponent controller = s_Held.GetKey(i);
   if (controller) controller.SetPilotCompartmentSlot(s_Held.GetElement(i).m_Seat);
  }
  s_Held.Clear();
 }

 // Vehicles deleted while held.
 protected static void Purge()
 {
  for (int i = s_Held.Count() - 1; i >= 0; i--)
   if (!s_Held.GetKey(i)) s_Held.RemoveElement(i);
 }
}
