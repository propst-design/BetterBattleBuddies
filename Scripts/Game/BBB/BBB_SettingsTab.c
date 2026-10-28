// The "Battle Buddies" tab in the game's Settings menu (BBB #8, 27 Oct 2026): choose, make,
// edit, rename, delete and export presets of every BBB option (BBB_Settings). The tab is added at
// run time with the tab view's own AddTab, so BBB doesn't override the Settings menu layout and
// keeps working beside mods that do (ACE replaces that layout to add its own tab).
// The rows are vanilla settings widgets (spin boxes and sliders) built from BBB_Settings, so a
// new option needs only its line in BBB_Settings.Define. Edits save to the preset file at once;
// the preset chosen here is the one the next singleplayer or hosted mission uses.

modded class SCR_SettingsSuperMenu
{
 static const ResourceName BBB_TAB = "{BBB05E7700000001}UI/layouts/BBB/BBB_SettingsTab.layout";

 override void OnMenuOpen()
 {
  super.OnMenuOpen();
  if (!m_SuperMenuComponent || !m_SuperMenuComponent.GetTabView()) return;
  m_SuperMenuComponent.GetTabView().AddTab(BBB_TAB, "Battle Buddies", identifier: "BBBSettings");
 }
}

// Shows a row's help when it gets focus or the mouse (spin boxes and sliders have no invoker for it).
class BBB_SettingsRowHandler : ScriptedWidgetEventHandler
{
 BBB_SettingsSubMenu m_Menu;
 string m_sKey;

 override bool OnFocus(Widget w, int x, int y)
 {
  if (m_Menu) m_Menu.Describe(m_sKey);
  return false;
 }

 override bool OnMouseEnter(Widget w, int x, int y)
 {
  if (m_Menu) m_Menu.Describe(m_sKey);
  return false;
 }
}

class BBB_SettingsSubMenu : SCR_SubMenuBase
{
 protected static const ResourceName LAYOUT_TITLE = "{FEEEB639F2735BA1}UI/layouts/Menus/SettingsMenu/CustomWidgets/SettingsTitle.layout";
 protected static const ResourceName LAYOUT_SPIN = "{C9DF0E6590F6C388}UI/layouts/WidgetLibrary/SpinBox/WLib_SpinBox.layout";
 protected static const ResourceName LAYOUT_SLIDER = "{4A41296C0E9A889F}UI/layouts/WidgetLibrary/WLib_Slider.layout";
 protected static const ResourceName LAYOUT_EDIT = "{0022F0B45ADBC5AC}UI/layouts/WidgetLibrary/EditBox/WLib_EditBox.layout";
 protected static const ResourceName LAYOUT_BUTTON = "{75C912A1C89BE6C2}UI/layouts/WidgetLibrary/Buttons/WLib_ButtonText.layout";
 // Rows that aren't options.
 protected static const string KEY_PRESET = "_preset";
 protected static const string KEY_NAME = "_name";

 protected Widget m_wContent;
 protected TextWidget m_wHeader;
 protected RichTextWidget m_wText;
 protected RichTextWidget m_wStatus;
 protected SCR_SpinBoxComponent m_PresetBox;
 protected SCR_EditBoxComponent m_NameBox;
 protected ref array<ref BBB_Preset> m_aPresets = {};
 protected BBB_Preset m_Preset;
 protected ref map<string, SCR_ChangeableComponentBase> m_Rows = new map<string, SCR_ChangeableComponentBase>();
 protected ref map<SCR_ChangeableComponentBase, string> m_Keys = new map<SCR_ChangeableComponentBase, string>();
 protected ref map<SCR_ButtonBaseComponent, string> m_Buttons = new map<SCR_ButtonBaseComponent, string>();
 protected ref array<ref BBB_SettingsRowHandler> m_aHandlers = {};
 protected bool m_bFilling;
 protected bool m_bDirty;
 protected string m_sDeleteAsked;
 protected string m_sStatus;

 override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
 {
  super.OnTabCreate(menuRoot, buttonsLayout, index);
  Widget root = GetRootWidget();
  m_wContent = root.FindAnyWidget("Content");
  m_wHeader = TextWidget.Cast(root.FindAnyWidget("DescriptionHeader"));
  m_wText = RichTextWidget.Cast(root.FindAnyWidget("DescriptionText"));
  m_wStatus = RichTextWidget.Cast(root.FindAnyWidget("Status"));
  if (!m_wContent) return;
  Build();
  Load(BBB_Settings.ActiveName());
  Describe("");
 }

 override void OnTabShow()
 {
  super.OnTabShow();
  GetGame().GetWorkspace().SetFocusedWidget(null);
 }

 override void OnTabHide()
 {
  SaveIfDirty();
  super.OnTabHide();
 }

 override void OnTabRemove()
 {
  SaveIfDirty();
  super.OnTabRemove();
 }

 // ---- building ----

 protected void Build()
 {
  AddTitle("Preset");
  m_PresetBox = AddSpin(KEY_PRESET, "Preset in use");
  if (m_PresetBox) m_PresetBox.m_OnChanged.Insert(OnPresetPicked);
  Widget edit = GetGame().GetWorkspace().CreateWidgets(LAYOUT_EDIT, m_wContent);
  if (edit)
  {
   AlignableSlot.SetPadding(edit, 4, 4, 4, 4);
   m_NameBox = SCR_EditBoxComponent.Cast(edit.FindHandler(SCR_EditBoxComponent));
   if (m_NameBox)
   {
    m_NameBox.SetLabel("Name");
    m_NameBox.m_OnConfirm.Insert(OnNameConfirmed);
    m_NameBox.m_OnWriteModeLeave.Insert(Rename);
   }
   Watch(edit, KEY_NAME);
  }
  Widget row = GetGame().GetWorkspace().CreateWidget(WidgetType.HorizontalLayoutWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 0, m_wContent);
  AlignableSlot.SetPadding(row, 4, 4, 4, 4);
  AddButton(row, "new", "New");
  AddButton(row, "copy", "Duplicate");
  AddButton(row, "delete", "Delete");
  AddButton(row, "export", "Export");
  AddButton(row, "reload", "Reload");
  foreach (string group : BBB_Settings.Groups())
  {
   AddTitle(group);
   foreach (BBB_Setting setting : BBB_Settings.All())
   {
    if (setting.m_sGroup == group) AddSetting(setting);
   }
  }
 }

 protected void AddTitle(string text)
 {
  Widget w = GetGame().GetWorkspace().CreateWidgets(LAYOUT_TITLE, m_wContent);
  if (!w) return;
  SCR_LabelComponent label = SCR_LabelComponent.FindLabelComponent(w);
  if (label) label.SetText(text);
 }

 protected SCR_SpinBoxComponent AddSpin(string key, string label)
 {
  Widget w = GetGame().GetWorkspace().CreateWidgets(LAYOUT_SPIN, m_wContent);
  if (!w) return null;
  AlignableSlot.SetPadding(w, 4, 4, 4, 4);
  SCR_SpinBoxComponent spin = SCR_SpinBoxComponent.Cast(w.FindHandler(SCR_SpinBoxComponent));
  if (!spin) return null;
  spin.SetLabel(label);
  spin.SetCycleMode(true);
  Watch(w, key);
  return spin;
 }

 protected void AddSetting(BBB_Setting setting)
 {
  if (setting.m_eKind == BBB_ESettingKind.NUMBER)
  {
   Widget w = GetGame().GetWorkspace().CreateWidgets(LAYOUT_SLIDER, m_wContent);
   if (!w) return;
   AlignableSlot.SetPadding(w, 4, 4, 4, 4);
   SCR_SliderComponent slider = SCR_SliderComponent.Cast(w.FindHandler(SCR_SliderComponent));
   if (!slider) return;
   slider.SetLabel(setting.m_sLabel);
   slider.SetSliderSettings(setting.m_fMin, setting.m_fMax, setting.m_fStep, "%1");
   slider.m_OnChanged.Insert(OnSlider);
   slider.GetOnChangedFinal().Insert(OnSliderFinal);
   m_Rows.Set(setting.m_sKey, slider);
   m_Keys.Set(slider, setting.m_sKey);
   Watch(w, setting.m_sKey);
   return;
  }
  SCR_SpinBoxComponent spin = AddSpin(setting.m_sKey, setting.m_sLabel);
  if (!spin) return;
  if (setting.m_eKind == BBB_ESettingKind.BOOL)
  {
   spin.AddItem("Off");
   spin.AddItem("On");
  }
  else
  {
   foreach (string label : setting.m_aLabels) spin.AddItem(label);
  }
  spin.m_OnChanged.Insert(OnSpin);
  m_Rows.Set(setting.m_sKey, spin);
  m_Keys.Set(spin, setting.m_sKey);
 }

 protected void AddButton(Widget parent, string key, string text)
 {
  Widget w = GetGame().GetWorkspace().CreateWidgets(LAYOUT_BUTTON, parent);
  if (!w) return;
  AlignableSlot.SetPadding(w, 2, 2, 2, 2);
  LayoutSlot.SetSizeMode(w, LayoutSizeMode.Fill);
  SCR_ButtonTextComponent button = SCR_ButtonTextComponent.FindButtonTextComponent(w);
  if (!button) return;
  button.SetText(text);
  button.m_OnClicked.Insert(OnButton);
  m_Buttons.Set(button, key);
  Watch(w, "_" + key);
 }

 protected void Watch(Widget w, string key)
 {
  BBB_SettingsRowHandler handler = new BBB_SettingsRowHandler();
  handler.m_Menu = this;
  handler.m_sKey = key;
  w.AddHandler(handler);
  m_aHandlers.Insert(handler);
 }

 // ---- showing a preset ----

 // Lists the presets again and picks one by name (the default when it is gone).
 protected void Load(string name)
 {
  m_aPresets = BBB_Preset.List();
  m_Preset = null;
  if (!m_PresetBox) return;
  m_bFilling = true;
  m_PresetBox.ClearAll();
  int pick = 0;
  foreach (int i, BBB_Preset preset : m_aPresets)
  {
   string shown = preset.m_sName;
   if (preset.m_bBuiltIn) shown += " (built-in)";
   m_PresetBox.AddItem(shown);
   if (preset.m_sName == name) pick = i;
  }
  m_PresetBox.SetCurrentItem(pick, false, false, false);
  m_bFilling = false;
  m_Preset = m_aPresets[pick];
  Show();
 }

 protected void Show()
 {
  if (!m_Preset) return;
  m_bFilling = true;
  if (m_NameBox) m_NameBox.SetValue(m_Preset.m_sName);
  foreach (BBB_Setting setting : BBB_Settings.All())
  {
   SCR_ChangeableComponentBase row = m_Rows.Get(setting.m_sKey);
   if (!row) continue;
   string value = m_Preset.Value(setting);
   SCR_SliderComponent slider = SCR_SliderComponent.Cast(row);
   if (slider)
   {
    slider.SetValue(value.ToFloat());
    slider.ShowCustomValue(value);
    continue;
   }
   SCR_SpinBoxComponent spin = SCR_SpinBoxComponent.Cast(row);
   if (!spin) continue;
   int index = 0;
   if (setting.m_eKind == BBB_ESettingKind.BOOL)
   {
    if (value == "1") index = 1;
   }
   else index = Math.Max(0, setting.m_aValues.Find(value));
   spin.SetCurrentItem(index, false, false, false);
  }
  m_bFilling = false;
  m_sDeleteAsked = "";
  Status("");
 }

 // ---- edits ----

 protected void OnPresetPicked(SCR_SpinBoxComponent spin, int index)
 {
  if (m_bFilling || index < 0 || index >= m_aPresets.Count()) return;
  SaveIfDirty();
  m_Preset = m_aPresets[index];
  BBB_Settings.SetActiveName(m_Preset.m_sName);
  Show();
 }

 protected void OnSpin(SCR_SpinBoxComponent spin, int index)
 {
  if (m_bFilling) return;
  BBB_Setting setting = BBB_Settings.Find(m_Keys.Get(spin));
  if (!setting || !m_Preset) return;
  string value = "0";
  if (setting.m_eKind == BBB_ESettingKind.BOOL)
  {
   if (index == 1) value = "1";
  }
  else if (index >= 0 && index < setting.m_aValues.Count()) value = setting.m_aValues[index];
  if (Refused()) return;
  m_Preset.SetValue(setting, value);
  m_bDirty = true;
  SaveIfDirty();
  Describe(setting.m_sKey);
 }

 protected void OnSlider(SCR_SliderComponent slider, float value)
 {
  if (m_bFilling) return;
  BBB_Setting setting = BBB_Settings.Find(m_Keys.Get(slider));
  if (!setting || !m_Preset) return;
  string shown = BBB_Settings.Number(Math.Round(value / setting.m_fStep) * setting.m_fStep);
  if (m_Preset.m_bBuiltIn)
  {
   // Shown as dragged; snapped back on release.
   slider.ShowCustomValue(shown);
   return;
  }
  m_Preset.SetValue(setting, shown);
  m_bDirty = true;
  slider.ShowCustomValue(shown);
 }

 protected void OnSliderFinal(SCR_SliderComponent slider, float value)
 {
  if (m_bFilling) return;
  if (Refused()) return;
  SaveIfDirty();
  Describe(m_Keys.Get(slider));
 }

 // Built-in presets stay as shipped: put the row back and say how to make one's own.
 protected bool Refused()
 {
  if (!m_Preset || !m_Preset.m_bBuiltIn) return false;
  GetGame().GetCallqueue().CallLater(Show, 0);
  Status("Built-in presets can't be changed. Press Duplicate to make your own copy, then edit that.");
  return true;
 }

 protected void SaveIfDirty()
 {
  if (!m_bDirty || !m_Preset) return;
  m_bDirty = false;
  if (!m_Preset.Save()) Status("Couldn't save " + m_Preset.m_sName + ".");
 }

 protected void OnNameConfirmed(SCR_EditBoxComponent edit, string text)
 {
  Rename(text);
 }

 protected void Rename(string text)
 {
  if (m_bFilling || !m_Preset) return;
  text = text.Trim();
  if (text == m_Preset.m_sName) return;
  if (m_Preset.m_bBuiltIn || text.IsEmpty())
  {
   if (m_Preset.m_bBuiltIn) Status("Built-in presets can't be renamed. Press Duplicate to make your own copy.");
   GetGame().GetCallqueue().CallLater(Show, 0);
   return;
  }
  BBB_Preset other = BBB_Preset.Find(text);
  string file = BBB_Settings.DIR + "/" + BBB_Preset.FileName(text) + ".json";
  bool taken = other && other.m_sFile != m_Preset.m_sFile;
  if (!taken && file != m_Preset.m_sFile && FileIO.FileExists(file)) taken = true;
  if (taken)
  {
   Status("A preset called " + text + " already exists.");
   GetGame().GetCallqueue().CallLater(Show, 0);
   return;
  }
  m_Preset.m_sName = text;
  m_bDirty = true;
  SaveIfDirty();
  BBB_Settings.SetActiveName(text);
  Load(text);
 }

 protected void OnButton(SCR_ButtonBaseComponent button)
 {
  string key = m_Buttons.Get(button);
  if (key == "new") Create(new BBB_Preset(), "My preset");
  else if (key == "copy" && m_Preset) Create(m_Preset.Copy(""), "Copy of " + m_Preset.m_sName);
  else if (key == "delete") Delete();
  else if (key == "export") Export();
  else if (key == "reload")
  {
   SaveIfDirty();
   string name = "";
   if (m_Preset) name = m_Preset.m_sName;
   Load(name);
   Status("Preset files read again from your profile folder.");
  }
 }

 protected void Create(BBB_Preset preset, string name)
 {
  SaveIfDirty();
  preset.m_sName = BBB_Preset.FreeName(name);
  if (!preset.Save())
  {
   Status("Couldn't save the new preset.");
   return;
  }
  BBB_Settings.SetActiveName(preset.m_sName);
  Load(preset.m_sName);
  Status("Made " + preset.m_sName + ". Its changes save as you make them.");
 }

 protected void Delete()
 {
  if (!m_Preset) return;
  if (m_Preset.m_bBuiltIn)
  {
   Status("Built-in presets can't be deleted.");
   return;
  }
  // Deleting can't be undone: the second press does it.
  if (m_sDeleteAsked != m_Preset.m_sName)
  {
   m_sDeleteAsked = m_Preset.m_sName;
   Status("Press Delete again to delete " + m_Preset.m_sName + " for good.");
   return;
  }
  string name = m_Preset.m_sName;
  m_bDirty = false;
  m_Preset.Delete();
  BBB_Settings.SetActiveName(BBB_Settings.DEFAULT_PRESET);
  Load(BBB_Settings.DEFAULT_PRESET);
  Status("Deleted " + name + ".");
 }

 protected void Export()
 {
  if (!m_Preset) return;
  SaveIfDirty();
  System.ExportToClipboard(m_Preset.Json());
  string pick = m_Preset.m_sName;
  string where;
  if (m_Preset.m_bBuiltIn) where = "Built-in presets need no file: start a server with -bbbPreset \"" + pick + "\".";
  else
  {
   pick = BBB_Preset.FileName(m_Preset.m_sName);
   where = "Saved as profile/BBB/Presets/" + pick + ".json in your Arma Reforger profile folder (Documents/My Games/ArmaReforger). For a server, put that file in the server's profile folder under BBB/Presets and start it with -bbbPreset " + pick + ".";
  }
  Status("Copied to the clipboard. " + where);
 }

 // ---- the text on the right ----

 void Describe(string key)
 {
  if (!m_wHeader || !m_wText) return;
  BBB_Setting setting = BBB_Settings.Find(key);
  if (setting)
  {
   string text = setting.m_sHelp + "\n\nDefault: " + setting.Shown(setting.m_sDefault);
   if (m_Preset && m_Preset.m_sName != BBB_Settings.DEFAULT_PRESET) text += ". In " + m_Preset.m_sName + ": " + setting.Shown(m_Preset.Value(setting));
   text += ".\nServer launch parameter: -" + setting.m_sKey + " " + setting.m_sDefault;
   string given;
   if (System.GetCLIParam(setting.m_sKey, given)) text += "\n\nThis game was started with -" + setting.m_sKey + " " + given + ", which overrides the preset.";
   m_wHeader.SetText(setting.m_sLabel);
   m_wText.SetText(text);
   return;
  }
  if (key == "_new") Help("New", "Makes a new preset with BBB's defaults.");
  else if (key == "_copy") Help("Duplicate", "Makes your own copy of this preset to edit. Built-in presets can only be copied.");
  else if (key == "_delete") Help("Delete", "Deletes this preset's file. Press twice.");
  else if (key == "_export") Help("Export", "Copies the preset to the clipboard to share it, and shows where its file is and how a server uses it.");
  else if (key == "_reload") Help("Reload", "Reads the preset files again, after you add or edit one outside the game.");
  else if (key == KEY_NAME) Help("Name", "The preset's name. Built-in presets keep theirs.");
  else Help("Better Battle Buddies", Overview());
 }

 protected void Help(string header, string text)
 {
  m_wHeader.SetText(header);
  m_wText.SetText(text);
 }

 protected string Overview()
 {
  string text = "Pick the preset your games use, or make your own and change any option. BBB default is what the mod ships with; Vanilla turns every BBB feature off.\n\nA preset applies when a mission starts, never in the middle of one. In singleplayer and games you host, the preset in use here applies. On someone else's server that server's settings apply. Server launch parameters (-bbb...) override a preset.";
  if (GetGame().GetWorld() && !GetGame().m_bIsMainMenuOpen)
  {
   if (!Replication.IsServer()) text += "\n\nYou're on someone else's server: its BBB settings apply in this game, not your presets.";
   else
   {
    string running = BBB_Settings.ActivePreset();
    if (running.IsEmpty()) running = BBB_Settings.DEFAULT_PRESET;
    text += "\n\nThis mission runs with " + running + ". Changes apply from the next mission.";
   }
  }
  return text;
 }

 protected void Status(string text)
 {
  m_sStatus = text;
  if (m_wStatus) m_wStatus.SetText(text);
 }
}
