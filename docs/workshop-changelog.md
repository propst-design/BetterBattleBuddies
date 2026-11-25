0.3.0

Faster attacking squad tactics, new Fidelity setting for smaller battles, smarter vehicle unsticking, safer pull-overs, true bullet-stopping cover, and sticky static gunners. Works with AC: Actual 0.7.0.

NEW

- Fidelity setting (-bbbFidelity 0|1|2 for Standard, High, Uncapped): spends more server time on smaller battles for faster squad-leader target sharing, tactic re-planning, grenadier/AT checks, and multi-ray shoulder cover tests. Steps down automatically above 30 and 60 AI (-bbbFidelityGovernor 0 to disable).
- Mod-maker hook MaxFidelity() lets a host mod cap the fidelity tier.

FIXED AND IMPROVED

- Attacking squad tactics no longer halt on first contact or freeze when the leader falls; against dug-in enemies at 70-250 m, half the squad flanks while the other half suppresses from the front.
- AI vehicle unstick (-bbbUnstick): looks before reversing instead of backing blind into queued vehicles, soldiers, or ditches; armed crews stay aboard and hold when boxed in, while soft-vehicle crews dismount on foot.
- Pulling over no longer tips trucks or APCs over: drivers brake on the road first, check verge slope and obstacles, ease onto the shoulder with stricter limits for tall/heavy vehicles, and stop immediately if the hull starts to roll or pitch.
- True cover (-bbbTrueCover): greenhouses, glass, and wire fences no longer count as bullet-stopping cover or holdable buildings.
- Defending squads keep the same gunner on static weapons instead of rotating him off every few minutes.
- Night flares now log why a pending flare request was called off.
