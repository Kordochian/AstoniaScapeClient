// Astonia 3.5 launcher: picks a server profile and starts bin\moac.exe with the right arguments.
// Build: build-launcher.bat (uses the C# compiler that ships with Windows / .NET Framework 4, C# 5).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("Astonia Launcher")]
[assembly: System.Reflection.AssemblyVersion("1.3.0.0")]

class Profile
{
    public string Name = "", Server = "127.0.0.1", Port = "", User = "", Password = "", Extra = "";
    public string Monitor = "", Fps = "", Threads = "";
    public ulong Options = ClientOption.Defaults;
    public bool RememberPassword;
}

// The client's -o bits (GO_* in src/astonia.h)
class ClientOption
{
    public int Bit;
    public string Label, Tip;

    ClientOption(int bit, string label, string tip) { Bit = bit; Label = label; Tip = tip; }

    // what the client picks when no -o is given (GO_DEFAULTS in sdl_core.c), plus sound
    public const ulong Defaults = (1ul << 1) | (1ul << 2) | (1ul << 5) | (1ul << 6) | (1ul << 10) | (1ul << 11) | (1ul << 13) | (1ul << 20);

    // bit 8 (true fullscreen) is set through the display mode instead
    public static readonly ClientOption[] All = {
        new ClientOption(6, "Sound", "Sound effects and music."),
        new ClientOption(7, "Large text", "Use the large font."),
        new ClientOption(17, "Fast lighting", "Simpler light calculations: faster on slow PCs, less pretty."),
        new ClientOption(14, "Brighter", "Increase gamma a little."),
        new ClientOption(15, "Even brighter", "Increase gamma more (can be combined with Brighter)."),
        new ClientOption(0, "Dark GUI", "Dark interface by Tegra."),
        new ClientOption(5, "Big health/mana bars", "Show the bigger health, endurance and mana bars."),
        new ClientOption(3, "Smaller bottom window", "Smaller bottom interface, more room for the map."),
        new ClientOption(4, "Sliding top window", "The top bar slides away when the mouse is not over it."),
        new ClientOption(16, "Top bar only at the edge", "The top bar slides out only when the mouse touches the window border."),
        new ClientOption(1, "Right-click menu", "Right-click context menu."),
        new ClientOption(2, "Action bar and key bindings", "The action bar and the new key bindings."),
        new ClientOption(9, "Old mouse wheel", "Use the legacy mouse wheel logic."),
        new ClientOption(19, "Wheel changes speed", "The mouse wheel switches between fast, normal and stealth movement."),
        new ClientOption(13, "Save minimaps", "Load and save the minimap between sessions."),
        new ClientOption(18, "No minimap", "Turn the minimap off completely."),
        new ClientOption(20, "Smooth camera", "Move the camera smoothly between server ticks (best at high frame rates)."),
        new ClientOption(10, "Faster feedback", "Process inventory and command feedback early for faster responses."),
        new ClientOption(11, "Short animation buffer", "Less command delay, more stutter in animations."),
        new ClientOption(12, "Use %APPDATA%", "Keep settings and logs in %APPDATA% instead of the client's bin\\data folder."),
    };
}

class LauncherForm : Form
{
    // display_mode values in the client's settings.ini (see src/gui/options.c)
    static readonly string[] displayKeys = { "", "windowed", "borderless", "fullscreen" };
    static readonly string[] displayNames = { "Client default", "Windowed", "Windowed fullscreen", "Fullscreen" };

    readonly string baseDir = AppDomain.CurrentDomain.BaseDirectory;
    readonly string iniPath;
    readonly List<Profile> profiles = new List<Profile>();
    ComboBox cbProfile, cbDisplay;
    TextBox tbServer, tbPort, tbUser, tbPass, tbWidth, tbHeight, tbExtra, tbMonitor, tbFps, tbThreads, tbPreview;
    CheckBox chkRemember;
    readonly List<CheckBox> optionBoxes = new List<CheckBox>();

    // Interface tab: also kept in the client's settings.ini (see src/gui/options.c, panels.c)
    static readonly string[] gfxNames = { "Auto (largest)", "1x (see the most)", "2x", "3x", "4x" };
    // 5% steps, the same as the zoom sliders in the client's Settings window
    static readonly int[] worldZooms = Steps(50, 300);
    static readonly int[] uiSizes = Steps(50, 200);
    static readonly int[] panelScales = Steps(75, 200);

    static int[] Steps(int lo, int hi)
    {
        var v = new int[(hi - lo) / 5 + 1];
        for (int i = 0; i < v.Length; i++) v[i] = lo + i * 5;
        return v;
    }
    static readonly string[] panelKeys = { "level", "equipment", "menu", "skills", "status", "chat", "inventory" };
    static readonly string[] panelNames = { "Level", "Equipment", "Menu", "Skills", "Effects", "Chat", "Inventory" };
    static readonly string[] chatNames = { "As \"Large text\"", "Normal", "Large" };
    ComboBox cbGfx, cbWorld, cbUi, cbChat;
    readonly ComboBox[] cbScale = new ComboBox[7];
    CheckBox chkLocked;
    bool resetLayout; // remove the saved panel positions and sizes at the next save
    ToolTip tips = new ToolTip();
    bool loading;
    string displayLoaded = ""; // display settings as last read from or written to settings.ini

    LauncherForm()
    {
        string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Astonia35Launcher");
        Directory.CreateDirectory(dir);
        iniPath = Path.Combine(dir, "profiles.ini");

        Text = "Astonia 3.5 Launcher";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(420, 480);
        Font = new Font("Segoe UI", 9f);
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

        // profile row above the tabs: both tabs belong to the selected profile
        AddLabel(this, "Profile:", 14);
        cbProfile = new ComboBox { Left = 110, Top = 14, Width = 170, DropDownStyle = ComboBoxStyle.DropDownList };
        cbProfile.SelectedIndexChanged += delegate { if (!loading) { ShowProfile(); } };
        Controls.Add(cbProfile);
        var bNew = new Button { Text = "New", Left = 288, Top = 13, Width = 58 };
        bNew.Click += delegate { NewProfile(); };
        var bDel = new Button { Text = "Delete", Left = 350, Top = 13, Width = 60 };
        bDel.Click += delegate { DeleteProfile(); };
        Controls.Add(bNew); Controls.Add(bDel);

        var tabs = new TabControl { Left = 8, Top = 46, Width = 404, Height = 386 };
        var tLogin = new TabPage("Login");
        var tOptions = new TabPage("Client options");
        var tInterface = new TabPage("Interface");
        tabs.TabPages.Add(tLogin); tabs.TabPages.Add(tOptions); tabs.TabPages.Add(tInterface);
        Controls.Add(tabs);

        BuildLoginTab(tLogin);
        BuildOptionsTab(tOptions);
        BuildInterfaceTab(tInterface);

        var bPlay = new Button { Text = "Play", Left = 256, Top = 442, Width = 75, Height = 28 };
        bPlay.Click += delegate { Play(); };
        var bSave = new Button { Text = "Save", Left = 337, Top = 442, Width = 75, Height = 28 };
        bSave.Click += delegate { StoreProfile(); SaveAll(); SaveDisplay(); };
        Controls.Add(bPlay); Controls.Add(bSave);
        AcceptButton = bPlay;

        LoadAll();
        if (profiles.Count == 0) profiles.Add(new Profile { Name = "Local server" });
        RefreshList(0);
    }

    void BuildLoginTab(TabPage t)
    {
        int y = 14;
        tbServer = AddField(t, "Server address:", y, 270);
        y += 30; tbPort = AddField(t, "Port (optional):", y, 70);
        y += 30; tbUser = AddField(t, "Character name:", y, 270);
        y += 30; tbPass = AddField(t, "Password:", y, 270); tbPass.UseSystemPasswordChar = true;
        y += 28; chkRemember = new CheckBox { Text = "Remember password (encrypted for this Windows user)", Left = 106, Top = y, Width = 285 };
        t.Controls.Add(chkRemember);
        y += 34; tbExtra = AddField(t, "Extra arguments:", y, 270);
        tips.SetToolTip(tbExtra, "Passed to moac.exe after everything else, so these win over the Client options tab.");
        y += 30;
        t.Controls.Add(new Label
        {
            Text = "Window size, display mode, large text, lighting and the other client switches are on the Client options tab.",
            Left = 10, Top = y, Width = 380, Height = 36, ForeColor = SystemColors.GrayText
        });
    }

    void BuildOptionsTab(TabPage t)
    {
        int y = 10;
        AddLabel(t, "Display mode:", y);
        cbDisplay = new ComboBox { Left = 106, Top = y, Width = 170, DropDownStyle = ComboBoxStyle.DropDownList };
        cbDisplay.Items.AddRange(displayNames);
        cbDisplay.SelectedIndexChanged += delegate { UpdateEnabled(); UpdatePreview(); };
        t.Controls.Add(cbDisplay);
        tips.SetToolTip(cbDisplay, "Shared with the in-game Options window: a change made in the game shows up here, and a change made here is used at the next start.");

        y += 30; tbWidth = AddField(t, "Size:", y, 56);
        t.Controls.Add(new Label { Text = "x", Left = 166, Top = y + 3, Width = 12 });
        tbHeight = new TextBox { Left = 180, Top = y, Width = 56 }; t.Controls.Add(tbHeight);
        t.Controls.Add(new Label { Text = "Monitor:", Left = 252, Top = y + 3, Width = 54 });
        tbMonitor = new TextBox { Left = 308, Top = y, Width = 30 }; t.Controls.Add(tbMonitor);
        tips.SetToolTip(tbWidth, "Window size, or the screen resolution in fullscreen (at least 640 x 360).");
        tips.SetToolTip(tbHeight, "Window size, or the screen resolution in fullscreen (at least 640 x 360).");
        tips.SetToolTip(tbMonitor, "Monitor to start on, 0 = the first. Empty = 0.");

        y += 32;
        var g = new GroupBox { Text = "Game options", Left = 8, Top = y, Width = 380, Height = 226 };
        t.Controls.Add(g);
        for (int i = 0; i < ClientOption.All.Length; i++)
        {
            var o = ClientOption.All[i];
            var cb = new CheckBox { Text = o.Label, Left = 10 + (i % 2) * 185, Top = 20 + (i / 2) * 20, Width = 182, Height = 20, Tag = o };
            cb.CheckedChanged += delegate
            {
                // %APPDATA% moves settings.ini, and with it the display settings
                if (o.Bit == 12 && !loading) LoadDisplay();
                UpdatePreview();
            };
            tips.SetToolTip(cb, o.Tip);
            optionBoxes.Add(cb);
            g.Controls.Add(cb);
        }

        y += 232;
        AddLabel(t, "Frame rate:", y);
        tbFps = new TextBox { Left = 106, Top = y, Width = 40 }; t.Controls.Add(tbFps);
        tips.SetToolTip(tbFps, "Frames per second. Empty = the monitor's refresh rate.");
        t.Controls.Add(new Label { Text = "Threads:", Left = 160, Top = y + 3, Width = 54 });
        tbThreads = new TextBox { Left = 216, Top = y, Width = 30 }; t.Controls.Add(tbThreads);
        tips.SetToolTip(tbThreads, "Background threads for loading graphics. Empty = 4, 0 = off.");
        var bDefaults = new Button { Text = "Defaults", Left = 306, Top = y - 1, Width = 82 };
        bDefaults.Click += delegate { SetOptionBoxes(ClientOption.Defaults); };
        tips.SetToolTip(bDefaults, "Tick the client's default game options (with sound).");
        t.Controls.Add(bDefaults);
        foreach (var tb in new[] { tbMonitor, tbFps, tbThreads }) tb.TextChanged += delegate { UpdatePreview(); };

        y += 28;
        tbPreview = new TextBox { Left = 8, Top = y, Width = 380, ReadOnly = true, BorderStyle = BorderStyle.None, BackColor = SystemColors.Control, ForeColor = SystemColors.GrayText };
        tips.SetToolTip(tbPreview, "Client options passed to moac.exe");
        t.Controls.Add(tbPreview);
    }

    static ComboBox AddCombo(Control parent, int left, int y, int width, IEnumerable<string> items)
    {
        var cb = new ComboBox { Left = left, Top = y, Width = width, DropDownStyle = ComboBoxStyle.DropDownList };
        foreach (var i in items) cb.Items.Add(i);
        cb.SelectedIndex = 0;
        parent.Controls.Add(cb);
        return cb;
    }

    static IEnumerable<string> Percents(int[] values)
    {
        foreach (int v in values) yield return v + "%";
    }

    // index of the value closest to v
    static int Nearest(int[] values, int v)
    {
        int best = 0;
        for (int i = 1; i < values.Length; i++) if (Math.Abs(values[i] - v) < Math.Abs(values[best] - v)) best = i;
        return best;
    }

    void BuildInterfaceTab(TabPage t)
    {
        int y = 10;
        AddLabel(t, "Graphics size:", y);
        cbGfx = AddCombo(t, 106, y, 150, gfxNames);
        tips.SetToolTip(cbGfx, "Makes everything larger, map and interface. Used when the client starts.");
        y += 30; AddLabel(t, "World zoom:", y);
        cbWorld = AddCombo(t, 106, y, 80, Percents(worldZooms));
        tips.SetToolTip(cbWorld, "Makes only the map larger; the panels keep their size.");
        y += 30; AddLabel(t, "Interface size:", y);
        cbUi = AddCombo(t, 106, y, 80, Percents(uiSizes));
        tips.SetToolTip(cbUi, "Makes all panels larger or smaller at once. Text gets blurry below 100%.");
        chkLocked = new CheckBox { Text = "Lock panels (no dragging)", Left = 200, Top = y, Width = 185 };
        t.Controls.Add(chkLocked);

        y += 34;
        var g = new GroupBox { Text = "Panel scale (text, items and buttons grow together)", Left = 8, Top = y, Width = 380, Height = 128 };
        t.Controls.Add(g);
        for (int i = 0; i < 7; i++)
        {
            int col = i / 4, row = i % 4;
            g.Controls.Add(new Label { Text = panelNames[i] + ":", Left = 10 + col * 190, Top = 24 + row * 25, Width = 76 });
            cbScale[i] = AddCombo(g, 88 + col * 190, 21 + row * 25, 70, Percents(panelScales));
        }

        y += 136; AddLabel(t, "Chat text:", y);
        cbChat = AddCombo(t, 106, y, 150, chatNames);
        tips.SetToolTip(cbChat, "Font size of the chat. Changing it in game clears the chat.");

        y += 34;
        var bReset = new Button { Text = "Reset panel layout", Left = 106, Top = y, Width = 150 };
        bReset.Click += delegate
        {
            resetLayout = true;
            foreach (var cb in cbScale) cb.SelectedIndex = Nearest(panelScales, 100);
            MessageBox.Show("The panels go back to their default places, sizes and scales when you press Save or Play.", Text);
        };
        tips.SetToolTip(bReset, "Puts every panel back where it started, at its normal size.");
        t.Controls.Add(bReset);

        y += 32;
        t.Controls.Add(new Label
        {
            Text = "These are the same settings as in the in-game Options window. Panels are moved by dragging their title and resized by the grip in their corner.",
            Left = 10, Top = y, Width = 380, Height = 36, ForeColor = SystemColors.GrayText
        });
    }

    static void AddLabel(Control parent, string text, int y) { parent.Controls.Add(new Label { Text = text, Left = 10, Top = y + 3, Width = 96 }); }

    TextBox AddField(Control parent, string label, int y, int width)
    {
        AddLabel(parent, label, y);
        var tb = new TextBox { Left = 106, Top = y, Width = width };
        parent.Controls.Add(tb);
        return tb;
    }

    Profile Current { get { int i = cbProfile.SelectedIndex; return i >= 0 && i < profiles.Count ? profiles[i] : null; } }

    ulong OptionBits()
    {
        ulong v = 0;
        foreach (var cb in optionBoxes) if (cb.Checked) v |= 1ul << ((ClientOption)cb.Tag).Bit;
        return v;
    }

    void SetOptionBoxes(ulong v)
    {
        foreach (var cb in optionBoxes) cb.Checked = (v & (1ul << ((ClientOption)cb.Tag).Bit)) != 0;
    }

    void UpdateEnabled()
    {
        // windowed fullscreen always uses the desktop resolution
        bool size = cbDisplay.SelectedIndex == 1 || cbDisplay.SelectedIndex == 3;
        tbWidth.Enabled = tbHeight.Enabled = size;
    }

    void UpdatePreview()
    {
        if (tbPreview != null && !loading) tbPreview.Text = "moac.exe ... " + ClientArgs(tbMonitor.Text, tbFps.Text, tbThreads.Text, OptionBits());
    }

    void RefreshList(int select)
    {
        loading = true;
        cbProfile.Items.Clear();
        foreach (var p in profiles) cbProfile.Items.Add(p.Name);
        cbProfile.SelectedIndex = Math.Max(0, Math.Min(select, profiles.Count - 1));
        loading = false;
        ShowProfile();
    }

    void ShowProfile()
    {
        var p = Current; if (p == null) return;
        loading = true;
        tbServer.Text = p.Server; tbPort.Text = p.Port; tbUser.Text = p.User; tbPass.Text = p.Password;
        tbExtra.Text = p.Extra; chkRemember.Checked = p.RememberPassword;
        tbMonitor.Text = p.Monitor; tbFps.Text = p.Fps; tbThreads.Text = p.Threads;
        SetOptionBoxes(p.Options);
        LoadDisplay();
        loading = false;
        UpdateEnabled();
        UpdatePreview();
    }

    void StoreProfile()
    {
        var p = Current; if (p == null) return;
        p.Server = tbServer.Text.Trim(); p.Port = tbPort.Text.Trim(); p.User = tbUser.Text.Trim(); p.Password = tbPass.Text;
        p.Extra = tbExtra.Text.Trim();
        p.RememberPassword = chkRemember.Checked;
        p.Monitor = tbMonitor.Text.Trim(); p.Fps = tbFps.Text.Trim(); p.Threads = tbThreads.Text.Trim();
        p.Options = OptionBits();
    }

    void NewProfile()
    {
        StoreProfile();
        string name = Prompt("Name of the new profile:", "Server " + (profiles.Count + 1));
        if (string.IsNullOrEmpty(name)) return;
        profiles.Add(new Profile { Name = name });
        RefreshList(profiles.Count - 1);
        SaveAll();
    }

    void DeleteProfile()
    {
        if (profiles.Count <= 1 || Current == null) return;
        if (MessageBox.Show("Delete profile \"" + Current.Name + "\"?", Text, MessageBoxButtons.YesNo) != DialogResult.Yes) return;
        profiles.RemoveAt(cbProfile.SelectedIndex);
        RefreshList(0);
        SaveAll();
    }

    static string Prompt(string text, string value)
    {
        var f = new Form { Text = "Astonia Launcher", ClientSize = new Size(300, 90), FormBorderStyle = FormBorderStyle.FixedDialog, StartPosition = FormStartPosition.CenterParent, MaximizeBox = false, MinimizeBox = false };
        var tb = new TextBox { Left = 10, Top = 30, Width = 280, Text = value };
        var ok = new Button { Text = "OK", Left = 215, Top = 58, Width = 75, DialogResult = DialogResult.OK };
        f.Controls.Add(new Label { Text = text, Left = 10, Top = 8, Width = 280 });
        f.Controls.Add(tb); f.Controls.Add(ok); f.AcceptButton = ok;
        return f.ShowDialog() == DialogResult.OK ? tb.Text.Trim() : null;
    }

    static string Quote(string s)
    {
        // Windows command-line quoting so names and passwords with spaces or quotes survive
        if (s.Length > 0 && s.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return s;
        var sb = new StringBuilder("\"");
        int bs = 0;
        foreach (char c in s)
        {
            if (c == '\\') { bs++; continue; }
            if (c == '"') { sb.Append('\\', bs * 2 + 1); sb.Append('"'); bs = 0; continue; }
            sb.Append('\\', bs); bs = 0; sb.Append(c);
        }
        sb.Append('\\', bs * 2);
        return sb.Append('"').ToString();
    }

    static int Number(string s, int min)
    {
        int n;
        return int.TryParse(s.Trim(), out n) && n >= min ? n : -1;
    }

    // Arguments from the Client options tab. The display mode and size are not passed:
    // the client reads them from settings.ini (see SaveDisplay).
    static string ClientArgs(string monitor, string fps, string threads, ulong options)
    {
        var args = new StringBuilder();
        int n;
        if ((n = Number(monitor, 1)) > 0) args.Append(" -n").Append(n);
        if ((n = Number(fps, 1)) > 0) args.Append(" -k").Append(n);
        if ((n = Number(threads, 0)) >= 0) args.Append(" -m").Append(n);
        args.Append(" -o").Append(options);
        return args.ToString().Trim();
    }

    void Play()
    {
        StoreProfile();
        SaveAll();
        if (!SaveDisplay()) return;
        var p = Current;
        string exe = Path.Combine(baseDir, "bin", "moac.exe");
        if (!File.Exists(exe)) { MessageBox.Show("bin\\moac.exe was not found. Put the launcher in the client folder (next to bin and res).", Text); return; }
        var gx1 = new FileInfo(Path.Combine(baseDir, "res", "gx1.zip"));
        // a git checkout without LFS leaves a tiny text pointer instead of the 174 MB archive
        if (!gx1.Exists || gx1.Length < 1000000)
        {
            MessageBox.Show("res\\gx1.zip (the main graphics, about 174 MB) is missing or only a placeholder. Copy it from the v1.1.3 windows_client.zip release into the res folder.", Text);
            return;
        }
        if (p.User == "" || p.Password == "" || p.Server == "") { MessageBox.Show("Enter server address, character name and password.", Text); return; }

        var args = new StringBuilder();
        // values attached to their flag (-uName): moac skips a separate value only if it does not start with '-'
        args.Append(Quote("-u" + p.User));
        args.Append(' ').Append(Quote("-p" + p.Password));
        args.Append(' ').Append(Quote("-d" + p.Server));
        args.Append(" -v35");
        int n;
        if (int.TryParse(p.Port, out n) && n > 0) args.Append(" -t").Append(n);
        args.Append(' ').Append(ClientArgs(p.Monitor, p.Fps, p.Threads, p.Options));
        if (p.Extra != "") args.Append(' ').Append(p.Extra);

        try
        {
            // the client loads res\ and writes bin\data relative to the working folder
            Process.Start(new ProcessStartInfo(exe, args.ToString()) { WorkingDirectory = baseDir, UseShellExecute = false });
            Close();
        }
        catch (Exception e) { MessageBox.Show("Could not start the client: " + e.Message, Text); }
    }

    // ---- profiles.ini (in %APPDATA%\Astonia35Launcher) ----

    static string Protect(string s)
    {
        if (s == "") return "";
        return Convert.ToBase64String(ProtectedData.Protect(Encoding.UTF8.GetBytes(s), null, DataProtectionScope.CurrentUser));
    }

    static string Unprotect(string s)
    {
        try { return s == "" ? "" : Encoding.UTF8.GetString(ProtectedData.Unprotect(Convert.FromBase64String(s), null, DataProtectionScope.CurrentUser)); }
        catch { return ""; }
    }

    void LoadAll()
    {
        if (!File.Exists(iniPath)) return;
        Profile p = null;
        foreach (string raw in File.ReadAllLines(iniPath, Encoding.UTF8))
        {
            string line = raw.Trim();
            if (line.StartsWith("[") && line.EndsWith("]")) { p = new Profile { Name = line.Substring(1, line.Length - 2) }; profiles.Add(p); continue; }
            int eq = line.IndexOf('=');
            if (p == null || eq < 0) continue;
            string k = line.Substring(0, eq).Trim(), v = line.Substring(eq + 1).Trim();
            ulong u;
            switch (k)
            {
                case "server": p.Server = v; break;
                case "port": p.Port = v; break;
                case "user": p.User = v; break;
                case "password": p.Password = Unprotect(v); p.RememberPassword = p.Password != ""; break;
                case "extra": p.Extra = v; break;
                case "monitor": p.Monitor = v; break;
                case "fps": p.Fps = v; break;
                case "threads": p.Threads = v; break;
                case "options": if (ulong.TryParse(v, out u)) p.Options = u; break;
            }
        }
    }

    void SaveAll()
    {
        var sb = new StringBuilder();
        foreach (var p in profiles)
        {
            sb.Append('[').Append(p.Name).Append("]\r\n");
            sb.Append("server=").Append(p.Server).Append("\r\n");
            sb.Append("port=").Append(p.Port).Append("\r\n");
            sb.Append("user=").Append(p.User).Append("\r\n");
            sb.Append("password=").Append(p.RememberPassword ? Protect(p.Password) : "").Append("\r\n");
            sb.Append("monitor=").Append(p.Monitor).Append("\r\n");
            sb.Append("fps=").Append(p.Fps).Append("\r\n");
            sb.Append("threads=").Append(p.Threads).Append("\r\n");
            sb.Append("options=").Append(p.Options).Append("\r\n");
            sb.Append("extra=").Append(p.Extra).Append("\r\n\r\n");
        }
        try { File.WriteAllText(iniPath, sb.ToString(), Encoding.UTF8); }
        catch (Exception e) { MessageBox.Show("Could not save profiles: " + e.Message, Text); }
    }

    // ---- display settings: the client's settings.ini, shared with the in-game Options window ----

    string SettingsPath()
    {
        // same place as the client: SDL_GetPrefPath(NULL, "Astonia") with -o bit 12, else bin\data
        if ((OptionBits() & (1ul << 12)) != 0)
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Astonia", "settings.ini");
        return Path.Combine(baseDir, "bin", "data", "settings.ini");
    }

    string DisplayState()
    {
        var sb = new StringBuilder();
        sb.Append(cbDisplay.SelectedIndex).Append('|').Append(tbWidth.Text.Trim()).Append('|').Append(tbHeight.Text.Trim());
        sb.Append('|').Append(cbGfx.SelectedIndex).Append('|').Append(cbWorld.SelectedIndex).Append('|').Append(cbUi.SelectedIndex);
        sb.Append('|').Append(chkLocked.Checked).Append('|').Append(cbChat.SelectedIndex).Append('|').Append(resetLayout);
        foreach (var cb in cbScale) sb.Append('|').Append(cb.SelectedIndex);
        return sb.ToString();
    }

    // settings.ini keys the launcher writes; panel_* positions only when the layout is reset
    bool Managed(string k)
    {
        if (k == "display_mode" || k == "width" || k == "height") return true;
        if (k == "zoom" || k == "world_zoom" || k == "ui_size" || k == "ui_locked" || k == "chat_text") return true;
        if (k.StartsWith("pscale_")) return true;
        return resetLayout && k.StartsWith("panel_");
    }

    void LoadDisplay()
    {
        string mode = "", w = "", h = "", path = SettingsPath();
        int gfx = 0, world = 100, ui = 100, locked = 0, chat = -1, n;
        var scale = new int[7];
        for (int i = 0; i < 7; i++) scale[i] = 100;
        try
        {
            if (File.Exists(path))
            {
                foreach (string raw in File.ReadAllLines(path))
                {
                    int eq = raw.IndexOf('=');
                    if (eq < 0 || raw.StartsWith("#") || raw.StartsWith(";")) continue;
                    string k = raw.Substring(0, eq).Trim(), v = raw.Substring(eq + 1).Trim();
                    if (k == "display_mode") mode = v;
                    else if (k == "width") w = v;
                    else if (k == "height") h = v;
                    else if (!int.TryParse(v, out n)) continue;
                    else if (k == "zoom") gfx = Math.Max(0, Math.Min(4, n));
                    else if (k == "world_zoom") world = n;
                    else if (k == "ui_size") ui = n;
                    else if (k == "ui_locked") locked = n;
                    else if (k == "chat_text") chat = Math.Max(-1, Math.Min(1, n));
                    else if (k.StartsWith("pscale_"))
                    {
                        int i = Array.IndexOf(panelKeys, k.Substring(7));
                        if (i >= 0) scale[i] = n;
                    }
                }
            }
        }
        catch { }
        bool was = loading;
        loading = true;
        cbDisplay.SelectedIndex = Math.Max(0, Array.IndexOf(displayKeys, mode));
        tbWidth.Text = w; tbHeight.Text = h;
        cbGfx.SelectedIndex = gfx;
        cbWorld.SelectedIndex = Nearest(worldZooms, world);
        cbUi.SelectedIndex = Nearest(uiSizes, ui);
        chkLocked.Checked = locked != 0;
        cbChat.SelectedIndex = chat + 1;
        for (int i = 0; i < 7; i++) cbScale[i].SelectedIndex = Nearest(panelScales, scale[i]);
        resetLayout = false;
        loading = was;
        UpdateEnabled();
        displayLoaded = DisplayState();
    }

    // Write the display settings into settings.ini if they were changed here. Other lines
    // (the volumes) stay as the client wrote them.
    bool SaveDisplay()
    {
        if (DisplayState() == displayLoaded) return true;
        int mode = cbDisplay.SelectedIndex, w = Number(tbWidth.Text, 640), h = Number(tbHeight.Text, 360);
        if ((mode == 1 || mode == 3) && (w < 0 || h < 0))
        {
            MessageBox.Show("Enter a window size or resolution of at least 640 x 360, or pick windowed fullscreen.", Text);
            return false;
        }
        string path = SettingsPath();
        var lines = new List<string>();
        try
        {
            if (File.Exists(path))
            {
                foreach (string raw in File.ReadAllLines(path))
                {
                    int eq = raw.IndexOf('=');
                    string k = eq < 0 ? "" : raw.Substring(0, eq).Trim();
                    if (!Managed(k)) lines.Add(raw);
                }
            }
            else lines.Add("# Astonia client settings, changed with the in-game Options menu");
            if (mode > 0)
            {
                lines.Add("display_mode=" + displayKeys[mode]);
                if (mode != 2) { lines.Add("width=" + w); lines.Add("height=" + h); }
            }
            lines.Add("zoom=" + cbGfx.SelectedIndex);
            lines.Add("world_zoom=" + worldZooms[cbWorld.SelectedIndex]);
            lines.Add("ui_size=" + uiSizes[cbUi.SelectedIndex]);
            lines.Add("ui_locked=" + (chkLocked.Checked ? 1 : 0));
            if (cbChat.SelectedIndex > 0) lines.Add("chat_text=" + (cbChat.SelectedIndex - 1));
            for (int i = 0; i < 7; i++)
            {
                int v = panelScales[cbScale[i].SelectedIndex];
                if (v != 100) lines.Add("pscale_" + panelKeys[i] + "=" + v);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllLines(path, lines.ToArray());
            resetLayout = false;
            displayLoaded = DisplayState();
            return true;
        }
        catch (Exception e)
        {
            MessageBox.Show("Could not save the display settings to " + path + ": " + e.Message, Text);
            return false;
        }
    }

    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.Run(new LauncherForm());
    }
}
