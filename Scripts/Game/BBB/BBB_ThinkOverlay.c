// On-screen side of BBB_Think, built from ordinary UI widgets: DbgUI and debug text/shapes
// draw nothing in the retail game (23 Oct 2026, Jacob saw no overlay with -bbbThink 1).
// Kept out of the way: a small translucent card on the right edge, a dot over the picked
// soldier and a red dot at his target's last seen spot. Everything hides while he is off
// screen or beyond pick range, or a menu or the map is open. Widgets ignore the cursor, so
// host-mod clicks (an RTS camera, a command map) pass through.
class BBB_ThinkOverlay
{
 static const int NOTES = 3;      // newest notes shown per soldier and per squad
 static const int MAX_LINES = 13;
 static const int MAX_CHARS = 72; // longer lines are cut, the card stays narrow
 protected static const ResourceName TEXT_LAYOUT = "{B537936D5B7E2BA1}UI/layouts/WidgetLibrary/TextWidgets/Text_BodySmall.layout";

 protected static Widget s_Root;
 protected static Widget s_Card;
 protected static Widget s_Marker;
 protected static Widget s_TargetMarker;
 protected static ref array<TextWidget> s_Texts;
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Texts = {};
 }
 protected static vector s_TargetAt;
 protected static bool s_bHasText;

 static bool HasText() { return s_Root && s_bHasText; }

 // Visible state (tests read it).
 static bool IsCardVisible() { return s_Card && s_Card.IsVisible(); }
 static string LineText(int index)
 {
  if (index < 0 || index >= s_Texts.Count() || !s_Texts[index].IsVisible()) return "";
  return s_Texts[index].GetText();
 }

 protected static bool Build()
 {
  if (s_Root) return true;
  WorkspaceWidget ws = GetGame().GetWorkspace();
  if (!ws) return false;
  s_Root = ws.CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR, Color.FromInt(Color.WHITE), 0, ws);
  FrameSlot.SetAnchorMin(s_Root, 0, 0);
  FrameSlot.SetAnchorMax(s_Root, 1, 1);
  FrameSlot.SetOffsets(s_Root, 0, 0, 0, 0);
  s_Root.SetZOrder(850);

  s_Card = ws.CreateWidget(WidgetType.OverlayWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR, Color.FromInt(Color.WHITE), 0, s_Root);
  FrameSlot.SetAnchorMin(s_Card, 1, 0.3);
  FrameSlot.SetAnchorMax(s_Card, 1, 0.3);
  FrameSlot.SetAlignment(s_Card, 1, 0);
  FrameSlot.SetPos(s_Card, -16, 0);
  FrameSlot.SetSizeToContent(s_Card, true);
  s_Card.SetOpacity(0.85);
  Widget background = ws.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR, new Color(0, 0, 0, 0.45), 0, s_Card);
  AlignableSlot.SetHorizontalAlign(background, LayoutHorizontalAlign.Stretch);
  AlignableSlot.SetVerticalAlign(background, LayoutVerticalAlign.Stretch);
  Widget column = ws.CreateWidget(WidgetType.VerticalLayoutWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR, Color.FromInt(Color.WHITE), 0, s_Card);
  AlignableSlot.SetPadding(column, 8, 6, 8, 6);
  for (int i = 0; i < MAX_LINES; i++)
  {
   TextWidget text = TextWidget.Cast(ws.CreateWidgets(TEXT_LAYOUT, column));
   if (!text) continue;
   text.SetFlags(WidgetFlags.IGNORE_CURSOR);
   text.SetExactFontSize(13);
   s_Texts.Insert(text);
  }

  s_Marker = CreateDot(ws, new Color(0.4, 0.85, 1.0, 0.9));
  s_TargetMarker = CreateDot(ws, new Color(1.0, 0.3, 0.25, 0.9));
  Hide();
  return true;
 }

 protected static Widget CreateDot(WorkspaceWidget ws, Color color)
 {
  Widget dot = ws.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR, color, 0, s_Root);
  FrameSlot.SetSize(dot, 6, 6);
  return dot;
 }

 // lines come from BBB_Think.Describe: name, "doing: ...", threat, cover/order, target, notes.
 static void SetText(notnull array<string> lines, vector targetAt)
 {
  if (!Build()) return;
  s_TargetAt = targetAt;
  array<string> shown = {};
  if (lines.Count() > 1)
  {
   string doing = lines[1];
   doing.Replace("doing: ", "");
   shown.Insert(lines[0] + "  |  " + doing);
  }
  for (int i = 2; i < lines.Count(); i++) shown.Insert(lines[i]);
  if (lines.Count() == 1) shown.Insert(lines[0]);
  for (int t = 0; t < s_Texts.Count(); t++)
  {
   TextWidget text = s_Texts[t];
   if (t >= shown.Count()) { text.SetVisible(false); continue; }
   string line = shown[t];
   if (line.Length() > MAX_CHARS) line = line.Substring(0, MAX_CHARS - 3) + "...";
   text.SetText(line);
   text.SetVisible(true);
   // The first line names him; section headers are dimmer than the notes under them.
   if (t == 0) text.SetColor(new Color(0.55, 0.88, 1.0, 1.0));
   else if (!line.StartsWith("  ")) text.SetColor(new Color(0.75, 0.75, 0.75, 1.0));
   else text.SetColor(Color.FromInt(Color.WHITE));
  }
  s_bHasText = true;
 }

 static void Place(World world, IEntity soldier)
 {
  if (!s_Root || !soldier) return;
  WorkspaceWidget ws = GetGame().GetWorkspace();
  if (!ws || Blocked()) { Hide(); return; }
  vector head = ws.ProjWorldToScreen(soldier.GetOrigin() + "0 2.1 0", world);
  if (!OnScreen(ws, head) || !InRange(world, soldier)) { Hide(); return; }
  s_Card.SetVisible(true);
  s_Marker.SetVisible(true);
  FrameSlot.SetPos(s_Marker, head[0] - 3, head[1] - 3);
  vector target;
  if (s_TargetAt != vector.Zero) target = ws.ProjWorldToScreen(s_TargetAt + "0 1 0", world);
  bool showTarget = s_TargetAt != vector.Zero && OnScreen(ws, target);
  s_TargetMarker.SetVisible(showTarget);
  if (showTarget) FrameSlot.SetPos(s_TargetMarker, target[0] - 3, target[1] - 3);
 }

 protected static bool OnScreen(WorkspaceWidget ws, vector screen)
 {
  if (screen[2] <= 0) return false;
  // The root fills the screen; its native size unscaled is the projection's reference frame.
  float width, height;
  s_Root.GetScreenSize(width, height);
  width = ws.DPIUnscale(width);
  height = ws.DPIUnscale(height);
  return screen[0] >= 0 && screen[0] <= width && screen[1] >= 0 && screen[1] <= height;
 }

 protected static bool InRange(World world, IEntity soldier)
 {
  vector camera[4];
  world.GetCurrentCamera(camera);
  return vector.Distance(camera[3], soldier.GetOrigin()) <= BBB_Think.PICK_RANGE_M;
 }

 protected static bool Blocked()
 {
  MenuManager menus = GetGame().GetMenuManager();
  if (menus && (menus.IsAnyMenuOpen() || menus.IsAnyDialogOpen())) return true;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  return mapEntity && mapEntity.IsOpen();
 }

 static void Hide()
 {
  if (s_Card) s_Card.SetVisible(false);
  if (s_Marker) s_Marker.SetVisible(false);
  if (s_TargetMarker) s_TargetMarker.SetVisible(false);
 }

 static void Remove()
 {
  if (s_Root) s_Root.RemoveFromHierarchy();
  s_Root = null;
  s_Card = null;
  s_Marker = null;
  s_TargetMarker = null;
  s_Texts.Clear();
  s_bHasText = false;
  s_TargetAt = vector.Zero;
 }
}
