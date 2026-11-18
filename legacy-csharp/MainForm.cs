using System.Drawing;
using System.Windows.Forms;

namespace LegacyMenuBridge;

public sealed class MainForm : Form
{
    private readonly TreeView _tree = new();
    private readonly Button _scanButton = new();
    private readonly Button _importButton = new();
    private readonly Button _selectAll = new();
    private readonly Button _clearAll = new();
    private readonly CheckBox _includeBuiltIn = new();
    private readonly Label _status = new();
    private List<LegacyEntry> _entries = new();
    private bool _suppressCheck;

    public MainForm()
    {
        Text = "LegacyMenuBridge";
        Width = 1000;
        Height = 660;
        MinimumSize = new Size(720, 420);
        StartPosition = FormStartPosition.CenterScreen;

        var top = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 46,
            Padding = new Padding(8),
            WrapContents = false
        };
        ConfigureButton(_scanButton, "Scan registry", 120);
        ConfigureButton(_importButton, "Import selected", 130);
        ConfigureButton(_selectAll, "Select all", 90);
        ConfigureButton(_clearAll, "Clear", 70);
        _includeBuiltIn.Text = "Include built-in entries";
        _includeBuiltIn.AutoSize = true;
        _includeBuiltIn.Margin = new Padding(12, 10, 0, 0);
        top.Controls.AddRange(new Control[] { _scanButton, _importButton, _selectAll, _clearAll, _includeBuiltIn });

        _tree.Dock = DockStyle.Fill;
        _tree.CheckBoxes = true;
        _tree.HideSelection = false;
        _tree.ShowLines = true;
        _tree.BorderStyle = BorderStyle.FixedSingle;

        _status.Dock = DockStyle.Bottom;
        _status.Height = 28;
        _status.TextAlign = ContentAlignment.MiddleLeft;
        _status.Padding = new Padding(8, 0, 8, 0);

        Controls.Add(_tree);
        Controls.Add(top);
        Controls.Add(_status);

        _scanButton.Click += async (_, _) => await ScanAsync();
        _importButton.Click += async (_, _) => await ImportAsync();
        _selectAll.Click += (_, _) => SetAll(true);
        _clearAll.Click += (_, _) => SetAll(false);
        _tree.AfterCheck += Tree_AfterCheck;
        Load += (_, _) => CheckEnvironment();
    }

    private static void ConfigureButton(Button b, string text, int width)
    {
        b.Text = text;
        b.Width = width;
        b.Height = 28;
        b.Margin = new Padding(0, 4, 8, 0);
    }

    private void CheckEnvironment()
    {
        string? folder = IkasWriter.FindCustomCommandsFolder();
        int build = Environment.OSVersion.Version.Build;
        string os = build >= 22000 ? "Windows 11" : $"Windows build {build} (older than 11)";
        string ikas = folder != null
            ? "Custom Context Menu found"
            : "Custom Context Menu (IKAS-MC) not detected - install it first";
        _status.Text = $"{os} | {ikas} | {folder ?? ""}";
        if (folder == null)
            _status.ForeColor = Color.Firebrick;
    }

    private async Task ScanAsync()
    {
        _scanButton.Enabled = false;
        _status.ForeColor = SystemColors.ControlText;
        _status.Text = "Scanning registry (both 32-bit and 64-bit views)...";
        try
        {
            bool includeBuiltIn = _includeBuiltIn.Checked;
            var entries = await Task.Run(() => RegistryScanner.Scan(includeBuiltIn));
            _entries = entries;
            PopulateTree(entries);
            int com = entries.Count(e => e.Kind == EntryKind.Com);
            int cmd = entries.Count - com;
            _status.Text = $"Found {entries.Count} legacy entries ({cmd} commands, {com} COM). Tick what you want, then Import.";
            Log.Write($"scan: found {entries.Count} entries ({cmd} commands, {com} COM)");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Scan failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            _status.Text = "Scan failed.";
        }
        finally { _scanButton.Enabled = true; }
    }

    private void PopulateTree(List<LegacyEntry> entries)
    {
        _tree.BeginUpdate();
        try
        {
            _tree.Nodes.Clear();
            foreach (var group in entries
                .GroupBy(e => string.IsNullOrWhiteSpace(e.Application) ? "Other" : e.Application)
                .OrderBy(g => g.Key, StringComparer.CurrentCultureIgnoreCase))
            {
                var parent = new TreeNode($"{group.Key}  ({group.Count()})");
                foreach (var e in group.OrderBy(x => x.Title, StringComparer.CurrentCultureIgnoreCase))
                {
                    string bitness = e.Bitness == Bitness.X86 ? " | x86" : e.Bitness == Bitness.X64 ? " | x64" : "";
                    string kind = e.Kind == EntryKind.Com ? "COM" : "cmd";
                    var child = new TreeNode($"{e.Title}  [{kind}, {ScopeText(e)}{bitness}]") { Tag = e };
                    parent.Nodes.Add(child);
                }
                _tree.Nodes.Add(parent);
            }
        }
        finally { _tree.EndUpdate(); }
    }

    private static string ScopeText(LegacyEntry e) => e.Scope switch
    {
        EntryScope.AllFiles => "all files",
        EntryScope.Folder => "folders",
        EntryScope.Background => "background",
        EntryScope.Drive => "drives",
        _ => e.Extensions.Count > 0 ? string.Join(" ", e.Extensions) : "file type"
    };

    private void Tree_AfterCheck(object? sender, TreeViewEventArgs e)
    {
        if (_suppressCheck || e.Node == null) return;
        _suppressCheck = true;
        try { SetChecked(e.Node, e.Node.Checked); }
        finally { _suppressCheck = false; }
    }

    private void SetChecked(TreeNode node, bool value)
    {
        node.Checked = value;
        foreach (TreeNode child in node.Nodes) SetChecked(child, value);
    }

    private void SetAll(bool value)
    {
        _suppressCheck = true;
        try
        {
            foreach (TreeNode node in _tree.Nodes) SetChecked(node, value);
        }
        finally { _suppressCheck = false; }
    }

    private List<LegacyEntry> CollectChecked()
    {
        var list = new List<LegacyEntry>();
        foreach (TreeNode parent in _tree.Nodes)
            foreach (TreeNode child in parent.Nodes)
                if (child.Checked && child.Tag is LegacyEntry entry) list.Add(entry);
        return list;
    }

    private async Task ImportAsync()
    {
        string? folder = IkasWriter.FindCustomCommandsFolder();
        if (folder == null)
        {
            MessageBox.Show(this,
                "Custom Context Menu (IKAS-MC) was not found. Install it, add one menu once so its folder exists, then try again.",
                "Custom Context Menu not found", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var selected = CollectChecked();
        if (selected.Count == 0)
        {
            _status.Text = "Nothing selected. Tick some entries first.";
            return;
        }

        _importButton.Enabled = false;
        _status.ForeColor = SystemColors.ControlText;
        _status.Text = $"Importing {selected.Count} entries...";
        try
        {
            var result = await Task.Run(() => BuildAndImport(selected, folder));
            string msg = $"Imported {result.Written}, skipped {result.Skipped} already-present.";
            if (result.Errors.Count > 0)
                msg += $"\r\n{result.Errors.Count} issue(s):\r\n" + string.Join("\r\n", result.Errors.Take(8));
            msg += "\r\n\r\nFinally, open Custom Context Menu and toggle its cache off and on to apply the changes.";
            _status.Text = $"Imported {result.Written}, skipped {result.Skipped}.";
            Log.Write($"import: wrote {result.Written}, skipped {result.Skipped}, errors {result.Errors.Count}");
            MessageBox.Show(this, msg, "Import complete", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            _status.Text = "Import failed.";
        }
        finally { _importButton.Enabled = true; }
    }

    private ImportResult BuildAndImport(List<LegacyEntry> selected, string folder)
    {
        var menus = new List<IkasMenu>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        // Simple commands and DelegateExecute verbs convert directly.
        foreach (var e in selected)
        {
            if (e.Kind == EntryKind.Command)
            {
                menus.Add(IkasWriter.FromCommand(e));
            }
            else if (!string.IsNullOrEmpty(e.Verb))
            {
                menus.Add(IkasWriter.FromDelegateVerb(e));
            }
        }

        // Real shellex handlers: read the classic menu once per scope/file-type/bitness.
        var groups = selected
            .Where(e => e.Kind == EntryKind.Com && string.IsNullOrEmpty(e.Verb))
            .GroupBy(e => (e.Scope, ExtKey(e), e.Bitness));

        foreach (var group in groups)
        {
            var owner = group.First();
            List<MenuItemInfo> items;
            if (owner.Scope == EntryScope.FileType)
            {
                string ext = owner.Extensions.FirstOrDefault() ?? ".txt";
                var specific = HandlerHost.InspectIsolated(CreateSampleFile(ext), owner.Bitness);
                var common = HandlerHost.InspectIsolated(CreateSampleFile(ProbeExt), owner.Bitness);
                var commonKeys = new HashSet<string>(common.Select(ItemKey), StringComparer.OrdinalIgnoreCase);
                items = specific.Where(i => !commonKeys.Contains(ItemKey(i))).ToList();
            }
            else
            {
                items = HandlerHost.InspectIsolated(ScopeSamplePath(owner), owner.Bitness);
            }

            foreach (var item in items)
            {
                string selector = !string.IsNullOrEmpty(item.Verb) ? item.Verb : item.Title;
                if (string.IsNullOrWhiteSpace(selector)) continue;
                string dedupeKey = $"{owner.Scope}|{ExtKey(owner)}|{owner.Bitness}|{selector}";
                if (!seen.Add(dedupeKey)) continue;

                var menu = IkasWriter.FromComItem(owner, item);
                menu.SourceId = owner.Id + "#" + selector;
                menus.Add(menu);
            }
        }

        return IkasWriter.Import(menus, folder);
    }

    private const string ProbeExt = ".lmbprobe";

    private static string ItemKey(MenuItemInfo i) =>
        !string.IsNullOrEmpty(i.Verb) ? "v:" + i.Verb : "t:" + i.Title;

    private static string ExtKey(LegacyEntry e) =>
        e.Extensions.Count == 0 ? "" : string.Join(",", e.Extensions.OrderBy(x => x, StringComparer.OrdinalIgnoreCase));

    private static string ScopeSamplePath(LegacyEntry e) => e.Scope switch
    {
        EntryScope.Folder => Path.GetTempPath(),
        EntryScope.Background => Path.GetTempPath(),
        EntryScope.Drive => Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\",
        _ => CreateSampleFile(ProbeExt)
    };

    private static string CreateSampleFile(string ext)
    {
        if (!ext.StartsWith('.')) ext = "." + ext;
        string path = Path.Combine(Path.GetTempPath(), "lmb-sample-" + Guid.NewGuid().ToString("N") + ext);
        try { File.WriteAllText(path, "LegacyMenuBridge sample"); } catch { }
        return path;
    }
}
