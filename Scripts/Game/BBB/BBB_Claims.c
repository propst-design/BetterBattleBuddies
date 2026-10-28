// Role claims (squad brain stage 1, Jacob 24 Oct): one BBB job per soldier at a time.
// A feature that moves, seats or holds a soldier takes him with Take() before it acts and
// renews the claim while its job runs (claims lapse after their TTL, so a missed Release
// cannot hold a man for good). A higher role takes a man from a lower one; the owner sees
// Can() turn false on its next tick and drops him the way it already does. Existing
// cross-feature guards stay, so in practice the first job keeps him unless a higher one
// needs him:
//   RESCUE (drag, medic on his way) > STATIC (walking to a static gun) > REARM (ammo / AT
//   run) > FIRE (FireManeuver bound or cover) > BUILDINGS (moving indoors / holding a room)
// Throws (grenade, smoke, flare) and foot gunner replacement are seconds long and claim
// nobody, but they never pick a man who is Committed() (RESCUE, STATIC or REARM).
// Withdrawal stays a group flag (BBB_Morale.IsWithdrawing), which every feature reads.
//  -bbbClaims 0   off: Can/Take/Holds always true, nothing is recorded
class BBB_Claim : Managed
{
 int m_iRole;
 float m_fUntil; // world time ms
 float m_fSince;
}

class BBB_Claims
{
 static const int NONE = 0;
 static const int BUILDINGS = 20;
 static const int FIRE = 30;
 static const int REARM = 40;
 static const int STATIC = 50;
 static const int RESCUE = 60;
 static const float TTL_MS = 3000;
 static bool s_bOn = true;
 protected static int s_iRead;
 protected static ref map<IEntity, ref BBB_Claim> s_Claims = new map<IEntity, ref BBB_Claim>();
 static ref array<IEntity> s_aVanilla = {}; // test only: soldiers who keep the old arbitration
 static int s_iTaken;     // tests read these
 static int s_iPreempted;
 static int s_iRefused;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbClaims", value) && value == "0");
  PrintFormat("[BBB] ROLE CLAIMS enabled=%1 ttl=%2s", s_bOn, TTL_MS / 1000);
 }

 static void Stop()
 {
  s_Claims.Clear();
  s_aVanilla.Clear();
 }

 static string Name(int role)
 {
  if (role == RESCUE) return "rescue";
  if (role == STATIC) return "static gun";
  if (role == REARM) return "rearm";
  if (role == FIRE) return "fire and manoeuvre";
  if (role == BUILDINGS) return "building";
  return "none";
 }

 protected static bool Off(IEntity soldier)
 {
  return !s_bOn || !soldier || s_aVanilla.Contains(soldier);
 }

 protected static float Now() { return GetGame().GetWorld().GetWorldTime(); }

 // His live claim's role (NONE: free).
 static int Role(IEntity soldier)
 {
  if (Off(soldier)) return NONE;
  BBB_Claim claim = s_Claims.Get(soldier);
  if (!claim || claim.m_fUntil <= Now()) return NONE;
  return claim.m_iRole;
 }

 // May this role have him? (free, already his, or held by a lower role)
 static bool Can(IEntity soldier, int role)
 {
  if (Off(soldier)) return true;
  int held = Role(soldier);
  return held == NONE || held <= role;
 }

 // Take or renew him for this role. False: a higher role has him.
 static bool Take(IEntity soldier, int role, float ttlMs = TTL_MS)
 {
  if (Off(soldier)) return true;
  int held = Role(soldier);
  if (held > role) { s_iRefused++; return false; }
  float now = Now();
  BBB_Claim claim = s_Claims.Get(soldier);
  if (!claim)
  {
   claim = new BBB_Claim();
   s_Claims.Set(soldier, claim);
  }
  if (held != role)
  {
   claim.m_fSince = now;
   s_iTaken++;
   if (held != NONE)
   {
    s_iPreempted++;
    PrintFormat("[BBB] CLAIM %1: %2 takes him from %3", soldier, Name(role), Name(held));
    BBB_Think.Note(soldier, "Claims", Name(role) + " takes him from " + Name(held));
   }
  }
  claim.m_iRole = role;
  claim.m_fUntil = now + ttlMs;
  return true;
 }

 // Does this role hold him now? (always true when off, so owners keep their old flow)
 static bool Holds(IEntity soldier, int role)
 {
  if (Off(soldier)) return true;
  return Role(soldier) == role;
 }

 static void Release(IEntity soldier, int role)
 {
  if (!soldier) return;
  BBB_Claim claim = s_Claims.Get(soldier);
  if (claim && claim.m_iRole == role) s_Claims.Remove(soldier);
 }

 // Too busy for a few seconds' extra task (throw, hop into a vehicle turret).
 static bool Committed(IEntity soldier)
 {
  return Role(soldier) >= REARM;
 }

 // Once a tick: drop lapsed claims and the dead (by index: a decayed key can't be looked up).
 static void Tick()
 {
  if (s_Claims.IsEmpty()) return;
  float now = Now();
  for (int i = s_Claims.Count() - 1; i >= 0; i--)
  {
   IEntity soldier = s_Claims.GetKey(i);
   BBB_Claim claim = s_Claims.GetElement(i);
   if (!soldier || !claim || claim.m_fUntil <= now || !BBB.IsAlive(soldier)) s_Claims.RemoveElement(i);
  }
  for (int v = s_aVanilla.Count() - 1; v >= 0; v--)
   if (!s_aVanilla[v]) s_aVanilla.Remove(v);
 }

 static int Count() { return s_Claims.Count(); }
}
