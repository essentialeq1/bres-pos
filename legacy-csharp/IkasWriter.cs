using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace LegacyMenuBridge;

/// <summary>A menu entry in the exact JSON shape IKAS-MC's Custom Context Menu reads.</summary>
public sealed class IkasMenu
{
    public string Title { get; set; } = "";
    public int Index { get; set; }
    public string Exe { get; set; } = "";
    public string Param { get; set; } = "";
    public string Icon { get; set; } = "";
    public string IconDark { get; set; } = "";
    public int AcceptDirectoryFlag { get; set; }
    public int AcceptFileFlag { get; set; }
    public string AcceptExts { get; set; } = "";
    public string AcceptFileRegex { get; set; } = "";
    public int AcceptMultipleFilesFlag { get; set; } = 1; // Each
    public int AcceptMultipleFilesRuleFlag { get; set; }
    public string PathDelimiter { get; set; } = "";
    public string ParamForMultipleFiles { get; set; } = "";
    public int ShowWindowFlag { get; set; }
    public int RunAsFlag { get; set; }
    public string WorkingDirectory { get; set; } = "";

    // Bookkeeping only. Never written into the JSON file.
    [JsonIgnore]
    public string SourceId { get; set; } = "";
}

public sealed class ImportResult
{
    public int Written { get; set; }
    public int Skipped { get; set; }
    public List<string> Errors { get; } = new();
}

/// <summary>Handles paths of the running tool and its optional 32-bit helper.</summary>
public static class Paths
{
    public static string SelfExe => Environment.ProcessPath ?? "";
    public static string SelfDir => Path.GetDirectoryName(SelfExe) ?? AppContext.BaseDirectory;
    public static string Helper32 => Path.Combine(SelfDir, "LegacyMenuBridge32.exe");

    public static string HelperFor(Bitness bitness)
        => bitness == Bitness.X86 && File.Exists(Helper32) ? Helper32 : SelfExe;
}

public static class IkasWriter
{
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    // IKAS-MC: FileMatchFlag  (None=0, Ext=1, Regex=2, ExtList=3, All=4)
    // IKAS-MC: DirectoryMatchFlag (Directory=1, Background=2, Desktop=4, Drive=8)
    private const int FileAll = 4;
    private const int FileExtList = 3;

    public static string? FindCustomCommandsFolder()
    {
        try
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string packages = Path.Combine(local, "Packages");
            if (Directory.Exists(packages))
            {
                foreach (var dir in Directory.GetDirectories(packages))
                {
                    string candidate = Path.Combine(dir, "LocalState", "custom_commands");
                    if (Directory.Exists(candidate)) return candidate;
                }
            }
            string alt = Path.Combine(local, "ContextMenuCustomApp", "custom_commands");
            if (Directory.Exists(alt)) return alt;
        }
        catch { }
        return null;
    }

    private static string LogPath
    {
        get
        {
            string dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "LegacyMenuBridge");
            Directory.CreateDirectory(dir);
            return Path.Combine(dir, "imported.txt");
        }
    }

    private static HashSet<string> LoadLog()
    {
        var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            if (File.Exists(LogPath))
                foreach (var line in File.ReadAllLines(LogPath))
                    if (!string.IsNullOrWhiteSpace(line)) set.Add(line.Trim());
        }
        catch { }
        return set;
    }

    private static void SaveLog(HashSet<string> set)
    {
        try { File.WriteAllLines(LogPath, set.OrderBy(x => x, StringComparer.OrdinalIgnoreCase)); }
        catch { }
    }

    // ---------------- menu factories ----------------

    /// <summary>Simple "shell\<verb>\command" entry run directly.</summary>
    public static IkasMenu FromCommand(LegacyEntry e)
    {
        string command = e.Command ?? "";
        var (exe, rest) = SplitExe(command);
        string ikasExe = QuoteIfNeeded(exe);
        string param = ConvertParam(rest);

        var menu = Base(e, e.Title, ikasExe, param);
        menu.Icon = ResolveIcon(e.Icon, exe);
        return menu;
    }

    /// <summary>A "shell\<verb>" entry that is really a shell verb (DelegateExecute).</summary>
    public static IkasMenu FromDelegateVerb(LegacyEntry e)
    {
        string exe = QuoteIfNeeded(Paths.HelperFor(e.Bitness));
        string param = $"--verb {Quote(e.Verb)} \"{{path}}\"";
        var menu = Base(e, e.Title, exe, param);
        menu.Icon = ResolveIcon(e.Icon, e.ModulePath);
        return menu;
    }

    /// <summary>One item discovered inside a COM handler (Kaspersky, 7-Zip, ...).</summary>
    public static IkasMenu FromComItem(LegacyEntry owner, MenuItemInfo item)
    {
        string selector = !string.IsNullOrEmpty(item.Verb) ? item.Verb : item.Title;
        string exe = QuoteIfNeeded(Paths.HelperFor(owner.Bitness));
        string param = $"--invoke {Quote(selector)} \"{{path}}\"";
        string title = string.IsNullOrEmpty(item.Title) ? (string.IsNullOrEmpty(item.Verb) ? owner.Title : item.Verb) : item.Title;
        var menu = Base(owner, title, exe, param);
        menu.Icon = ResolveIcon("", owner.ModulePath);
        return menu;
    }

    private static IkasMenu Base(LegacyEntry e, string title, string exe, string param)
    {
        var menu = new IkasMenu
        {
            Title = string.IsNullOrWhiteSpace(title) ? "Unnamed" : title.Trim(),
            Exe = exe,
            Param = param,
            AcceptMultipleFilesFlag = 1, // Each
            ShowWindowFlag = 0,          // Show Normal
            RunAsFlag = 0,               // Default
            SourceId = e.Id
        };
        ApplyScope(menu, e);
        return menu;
    }

    private static void ApplyScope(IkasMenu menu, LegacyEntry e)
    {
        switch (e.Scope)
        {
            case EntryScope.AllFiles:
                menu.AcceptFileFlag = FileAll;
                menu.AcceptExts = "*";
                menu.AcceptDirectoryFlag = 0;
                break;
            case EntryScope.FileType:
                menu.AcceptFileFlag = FileExtList;
                menu.AcceptExts = string.Join("|", e.Extensions.Select(x => x.ToLowerInvariant()));
                menu.AcceptDirectoryFlag = 0;
                break;
            case EntryScope.Folder:
                menu.AcceptFileFlag = 0;
                menu.AcceptExts = "";
                menu.AcceptDirectoryFlag = 1; // Directory
                break;
            case EntryScope.Background:
                menu.AcceptFileFlag = 0;
                menu.AcceptExts = "";
                menu.AcceptDirectoryFlag = 6; // Background | Desktop
                break;
            case EntryScope.Drive:
                menu.AcceptFileFlag = 0;
                menu.AcceptExts = "";
                menu.AcceptDirectoryFlag = 8; // Drive
                break;
        }
    }

    // ---------------- import ----------------

    /// <summary>Writes the menus into the IKAS-MC folder, skipping already-imported ids.</summary>
    public static ImportResult Import(IEnumerable<IkasMenu> menus, string folder)
    {
        var result = new ImportResult();
        var log = LoadLog();
        try
        {
            Directory.CreateDirectory(folder);
            int index = 0;
            foreach (var menu in menus)
            {
                try
                {
                    if (!string.IsNullOrEmpty(menu.SourceId) && log.Contains(menu.SourceId))
                    {
                        result.Skipped++;
                        index++;
                        continue;
                    }

                    menu.Index = index++;
                    string json = JsonSerializer.Serialize(menu, JsonOpts);
                    string path = UniquePath(folder, SanitizeFileName(menu.Title));
                    File.WriteAllText(path, json, new UTF8Encoding(false));
                    result.Written++;

                    if (!string.IsNullOrEmpty(menu.SourceId)) log.Add(menu.SourceId);
                }
                catch (Exception ex)
                {
                    result.Errors.Add($"{menu.Title}: {ex.Message}");
                    index++;
                }
            }
            SaveLog(log);
        }
        catch (Exception ex)
        {
            result.Errors.Add(ex.Message);
        }
        return result;
    }

    // ---------------- helpers ----------------

    private static (string exe, string rest) SplitExe(string command)
    {
        command = (command ?? "").Trim();
        if (command.Length == 0) return ("", "");
        if (command.StartsWith('"'))
        {
            int end = command.IndexOf('"', 1);
            if (end > 0)
            {
                string exe = command.Substring(1, end - 1);
                string rest = end + 1 < command.Length ? command[(end + 1)..].TrimStart() : "";
                return (exe, rest);
            }
        }
        int sp = command.IndexOf(' ');
        if (sp > 0) return (command[..sp], command[(sp + 1)..].TrimStart());
        return (command, "");
    }

    private static string ConvertParam(string args)
    {
        if (string.IsNullOrEmpty(args)) return "";
        var sb = new StringBuilder(args);
        sb.Replace("%1", "{path}");
        sb.Replace("%L", "{path}");
        sb.Replace("%l", "{path}");
        sb.Replace("%V", "{path}");
        sb.Replace("%v", "{path}");
        sb.Replace("%*", "{path}");
        sb.Replace("%D", "{parent}");
        sb.Replace("%d", "{parent}");
        sb.Replace("%H", "");
        sb.Replace("%h", "");
        return sb.ToString().Trim();
    }

    private static string QuoteIfNeeded(string value)
    {
        value = (value ?? "").Trim();
        if (value.Length == 0) return value;
        if (value.StartsWith('"') && value.EndsWith('"') && value.Length >= 2) return value;
        return value.Contains(' ') ? $"\"{value}\"" : value;
    }

    private static string Quote(string value) => $"\"{(value ?? "").Replace("\"", "\\\"")}\"";

    private static string ResolveIcon(string icon, string exeOrModule)
    {
        if (!string.IsNullOrWhiteSpace(icon)) return icon.Trim();
        string target = (exeOrModule ?? "").Trim().Trim('"');
        int comma = target.LastIndexOf(',');
        if (comma > 0 && int.TryParse(target[(comma + 1)..], out _)) target = target[..comma];
        if (target.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
            target.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
            return $"{QuoteIfNeeded(target)},0";
        return "";
    }

    private static string SanitizeFileName(string title)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder();
        foreach (var ch in title)
            sb.Append(Array.IndexOf(invalid, ch) >= 0 ? '_' : ch);
        string name = sb.ToString().Trim().TrimEnd('.');
        if (name.Length == 0) name = "menu";
        if (name.Length > 80) name = name[..80];
        return name;
    }

    private static string UniquePath(string folder, string baseName)
    {
        string path = Path.Combine(folder, baseName + ".json");
        int n = 2;
        while (File.Exists(path))
        {
            path = Path.Combine(folder, $"{baseName} ({n}).json");
            n++;
        }
        return path;
    }
}
