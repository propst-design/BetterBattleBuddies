0.2.1

Bug fixes for grenadiers, fall-backs, Game Master orders and static guns. Works with AC: Actual 0.6.1.

FIXED

- Grenadiers no longer waste 40 mm grenades on targets too close for them to arm: inside the arming range plus 5 m they use their rifle or sidearm, and a man with only a grenade launcher (such as an M79) backs off first. This also applies while a squad leader or a vehicle gunner holds the target. The arming distance is read from each grenade, so modded rounds are covered. Option: -bbbGLArming 0.
- Game Master orders come first for morale: a squad on a Game Master waypoint no longer breaks, falls back or surrenders, and a break under way ends when the Game Master gives it an order.
- Game Master and Conflict move and defend waypoints now get the same order leash and squad tactics as the base ones.
- A squad falling back, after its morale breaks or because the host mod ordered it, no longer drops prone or crouches around enemy armour: the withdrawal goes on. A host mod's own fall-back order now takes over from a morale break already under way.
- Soldiers sent prone by nearby enemy armour get up as soon as a player joins or takes over their squad.
- Soldiers stuck in a doorway no longer swing the door open and shut every minute or so.
- A soldier on his way to man a static gun turns back with his squad when it falls back, and a wounded gunner is no longer put back on the gun while his replacement climbs on.
- With the optional hot-vehicle setting, player-led and host-controlled gunners and vehicles are left alone by its reload, re-attack and slow-down rules too.
- A deleted Defend waypoint no longer logs an error, and a possible script error when the last man of a squad surrenders is fixed.
