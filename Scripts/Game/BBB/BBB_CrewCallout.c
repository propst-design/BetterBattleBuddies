// Crew call-out (6 Nov 2026; Jacob: "if the driver or commander could say 'gunner, there's 10 guys
// at 1 o'clock, shoot them'"). Vanilla: every crewman spots for himself and a gunner shoots only what
// he has recognised himself; a BTR-70's commander saw four riflemen for 65 s that the gunner beside
// him never did (Hot AI trace). Hot AI's fire control had a call-out already, only with
// -bbbHotVehicles on and for the nearest sighting. This one has its own switch, on by default, and
// calls out the biggest threat:
//  - the crew's fresh sightings: enemies any AI crewman but the gunner recognised within SEEN_S
//    (one man seen by two crewmen counts once);
//  - grouped: each sighting with every other within GROUP_M of it, scored MAN_SCORE a man,
//    SHOOTING_SCORE more for one who shot at the crew lately, less RANGE_PENALTY a metre, so ten men
//    at 300 m beat one man at 80 m;
//  - the gunner, if he has no target in sight, turns onto it (vanilla's commander look priority) and,
//    if after BBB_HotAI.CALLOUT_FIRE_S he still recognises nothing of his own, puts a short series of
//    fire (BBB_HotAI's) into a box round the group. The box is centred on a real man's last seen spot
//    (the one with the most of his group round him), never a guessed point, and sized to the group.
//    Aimed fire takes over as soon as he recognises a target.
// Runs inside BBB_HotAI's fire control loop (once a second, AI turret gunners only) with its gates:
// hold fire, a player in the group or seat, the host's LeaveAlone, BBB_OrderIntent.GunnerBlocks.
//  -bbbCrewCallout 0   off (with Hot AI gunners and vehicles on, the older nearest-man call-out)
class BBB_CrewCallout
{
 static bool s_bOn;
 protected static int s_iRead;
 static int s_iCallouts;   // fire series started on a call-out (tests)
 static int s_iLastMen;    // men in the group last called out (tests and the log)

 static const float SEEN_S = 4;
 static const float GROUP_M = 25;
 static const float MAN_SCORE = 100;
 static const float SHOOTING_SCORE = 150;
 static const float ENDANGER_S = 10;
 static const float RANGE_PENALTY = 0.1;
 static const int MAX_SPOTS = 32;

 static bool On()
 {
  if (BBB_Settings.Due(s_iRead))
  {
   string value;
   s_bOn = !(BBB_Settings.Get("bbbCrewCallout", value) && value == "0");
   PrintFormat("[BBB] CREW CALLOUT enabled=%1", s_bOn);
  }
  return s_bOn;
 }

 // The biggest group the gunner's AI crewmates see. aim: the last seen spot of the man with most of
 // his group round him; men: how many in that group; spread: farthest of them from aim (m).
 static bool Pick(IEntity vehicle, IEntity gunner, array<BaseTarget> targets, out vector aim, out int men, out float spread)
 {
  BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
  if (!compartments) return false;
  array<BaseCompartmentSlot> slots = {};
  compartments.GetCompartments(slots);
  array<IEntity> enemies = {};
  array<vector> spots = {};
  array<float> ages = {};
  array<bool> shooting = {};
  foreach (BaseCompartmentSlot slot : slots)
  {
   IEntity mate = slot.GetOccupant();
   if (!mate || mate == gunner || BBB.PlayerOf(mate) || !BBB.IsAlive(mate)) continue;
   PerceptionComponent eyes = PerceptionComponent.Cast(mate.FindComponent(PerceptionComponent));
   if (!eyes) continue;
   targets.Clear();
   eyes.GetTargetsList(targets, ETargetCategory.ENEMY);
   foreach (BaseTarget target : targets)
   {
    if (!target || target.IsDisarmed()) continue;
    float age = target.GetTimeSinceSeen();
    if (age > SEEN_S) continue;
    IEntity enemy = target.GetTargetEntity();
    if (!enemy || BBB_SuppressionSafety.AircraftSource(enemy)) continue;
    bool shot = target.GetTimeSinceEndangered() < ENDANGER_S;
    int known = enemies.Find(enemy);
    if (known >= 0)
    {
     if (age < ages[known])
     {
      ages[known] = age;
      spots[known] = target.GetLastSeenPosition();
     }
     if (shot) shooting[known] = true;
     continue;
    }
    if (enemies.Count() >= MAX_SPOTS) continue;
    enemies.Insert(enemy);
    spots.Insert(target.GetLastSeenPosition());
    ages.Insert(age);
    shooting.Insert(shot);
   }
  }
  targets.Clear();
  int count = spots.Count();
  if (count == 0) return false;
  vector at = vehicle.GetOrigin();
  float bestScore = -float.MAX;
  int best = -1;
  for (int i = 0; i < count; i++)
  {
   float score = -RANGE_PENALTY * vector.DistanceXZ(at, spots[i]);
   int group = 0;
   float far = 0;
   for (int j = 0; j < count; j++)
   {
    float apart = vector.DistanceXZ(spots[i], spots[j]);
    if (apart > GROUP_M) continue;
    group++;
    score += MAN_SCORE;
    if (shooting[j]) score += SHOOTING_SCORE;
    if (apart > far) far = apart;
   }
   if (score <= bestScore) continue;
   bestScore = score;
   best = i;
   men = group;
   spread = far;
  }
  aim = spots[best];
  s_iLastMen = men;
  return true;
 }
}
