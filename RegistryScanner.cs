using System.Diagnostics;
using Microsoft.Win32;

namespace LegacyMenuBridge;

/// <summary>
/// Reads legacy ("classic") context-menu entries straight from the registry.
/// Both the 64-bit and 32-bit registry views are read so handlers that only
/// exist in one view are not missed.
/// </summary>
public static class RegistryScanner
{
    // Verbs that Windows' own modern menu already shows. Skipped unless the
    // caller asks for built-in entries too.
    private static readonly HashSet<string> SkipVerbs = new(StringComparer.OrdinalIgnoreCase)
    {
        "open", "edit", "print", "printto", "runas", "runasuser", "explore", "find",
        "properties", "play", "preview", "openas", "opennew", "pin", "pintostart",
        "pinfromstart", "troubleshootcompatibility", "compatibility", "modernsharing"
    };

    private static readonly RegistryView[] Views = { RegistryView.Registry64, RegistryView.Registry32 };

    public static List<LegacyEntry> Scan(bool includeBuiltIn = false)
    {
        var result = new List<LegacyEntry>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        // 1. Simple "shell\<verb>" entries on the fixed scope roots.
        ScanVerbRoot(result, seen, @"*\shell", EntryScope.AllFiles, Array.Empty<string>(), includeBuiltIn);
        ScanVerbRoot(result, seen, @"Directory\shell", EntryScope.Folder, Array.Empty<string>(), includeBuiltIn);
        ScanVerbRoot(result, seen, @"Directory\Background\shell", EntryScope.Background, Array.Empty<string>(), includeBuiltIn);
        ScanVerbRoot(result, seen, @"Drive\shell", EntryScope.Drive, Array.Empty<string>(), includeBuiltIn);

        // 2. COM handlers ("shellex\ContextMenuHandlers") on the fixed scope roots.
        ScanComRoot(result, seen, @"*\shellex\ContextMenuHandlers", EntryScope.AllFiles, Array.Empty<string>());
        ScanComRoot(result, seen, @"Directory\shellex\ContextMenuHandlers", EntryScope.Folder, Array.Empty<string>());
        ScanComRoot(result, seen, @"Directory\Background\shellex\ContextMenuHandlers", EntryScope.Background, Array.Empty<string>());
        ScanComRoot(result, seen, @"Drive\shellex\ContextMenuHandlers", EntryScope.Drive, Array.Empty<string>());

        // 3. Per-extension entries (".ext\shell" and ".ext\shellex\ContextMenuHandlers").
        ScanPerExtension(result, seen, includeBuiltIn);

        return MergeCom(result);
    }

    private static void ScanVerbRoot(List<LegacyEntry> result, HashSet<string> seen,
        string subPath, EntryScope scope, string[] exts, bool includeBuiltIn)
    {
        foreach (var view in Views)
        {
            using var root = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, view);
            using var shellKey = root.OpenSubKey(subPath);
            if (shellKey == null) continue;
            ReadVerbChildren(result, seen, shellKey, $@"HKCR\{subPath}", scope, exts, includeBuiltIn);
        }
    }

    private static void ReadVerbChildren(List<LegacyEntry> result, HashSet<string> seen,
        RegistryKey shellKey, string registryPath, EntryScope scope, string[] exts, bool includeBuiltIn)
    {
        foreach (var verb in shellKey.GetSubKeyNames())
        {
            using var verbKey = shellKey.OpenSubKey(verb);
            if (verbKey == null) continue;

            using var commandKey = verbKey.OpenSubKey("command");
            string command = commandKey?.GetValue(null) as string ?? "";
            string delegateExecute = verbKey.GetValue("DelegateExecute") as string ?? "";

            if (command.Length == 0 && delegateExecute.Length == 0) continue; // not runnable
            if (!includeBuiltIn && SkipVerbs.Contains(verb)) continue;

            string entryId = $"{scope}:{registryPath}\\{verb}";
            if (!seen.Add(entryId)) continue;

            string mui = verbKey.GetValue("MUIVerb") as string ?? "";
            string icon = verbKey.GetValue("Icon") as string ?? "";

            var entry = new LegacyEntry
            {
                Id = entryId,
                Title = CleanTitle(mui, verb),
                RegistryPath = $@"{registryPath}\{verb}",
                Scope = scope,
                Extensions = new List<string>(exts),
                Icon = icon,
                Selected = true
            };

            if (command.Length > 0)
            {
                entry.Kind = EntryKind.Command;
                entry.Command = command;
                entry.Application = AppFromCommandLine(command, icon);
            }
            else
            {
                // DelegateExecute: no command line, run it through the shell verb.
                entry.Kind = EntryKind.Com;
                entry.Verb = verb;
                entry.Clsid = delegateExecute;
                entry.Application = AppFromClsid(delegateExecute) ?? Prettify(verb);
            }

            result.Add(entry);
        }
    }

    private static void ScanComRoot(List<LegacyEntry> result, HashSet<string> seen,
        string subPath, EntryScope scope, string[] exts)
    {
        foreach (var view in Views)
        {
            using var root = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, view);
            using var comKey = root.OpenSubKey(subPath);
            if (comKey == null) continue;
            ReadComChildren(result, seen, comKey, $@"HKCR\{subPath}", scope, exts);
        }
    }

    private static void ReadComChildren(List<LegacyEntry> result, HashSet<string> seen,
        RegistryKey handlersKey, string registryPath, EntryScope scope, string[] exts)
    {
        foreach (var name in handlersKey.GetSubKeyNames())
        {
            using var handlerKey = handlersKey.OpenSubKey(name);
            if (handlerKey == null) continue;

            string clsid = handlerKey.GetValue(null) as string ?? "";
            string trimmedName = name.Trim();
            if (clsid.Length == 0 && trimmedName.StartsWith("{") && trimmedName.EndsWith("}"))
                clsid = trimmedName;
            if (clsid.Length == 0) continue;

            string entryId = $"com:{scope}:{clsid}";
            if (!seen.Add(entryId)) continue;

            var (clsName, module) = ResolveClsid(clsid);
            result.Add(new LegacyEntry
            {
                Id = entryId,
                Title = CleanTitle(name, clsName ?? name),
                Application = clsName ?? AppFromModule(module) ?? Prettify(name),
                Kind = EntryKind.Com,
                Scope = scope,
                Extensions = new List<string>(exts),
                RegistryPath = $@"{registryPath}\{name}",
                Clsid = clsid,
                ModulePath = module ?? "",
                Bitness = GetFileBitness(module),
                Icon = module ?? "",
                Selected = true
            });
        }
    }

    private static void ScanPerExtension(List<LegacyEntry> result, HashSet<string> seen, bool includeBuiltIn)
    {
        var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var view in Views)
        {
            using var root = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, view);
            foreach (var name in root.GetSubKeyNames())
            {
                if (name.StartsWith('.')) extensions.Add(name);
            }
        }

        foreach (var ext in extensions)
        {
            var exts = new[] { ext };
            foreach (var view in Views)
            {
                using var root = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, view);
                using (var shell = root.OpenSubKey($@"{ext}\shell"))
                {
                    if (shell != null)
                        ReadVerbChildren(result, seen, shell, $@"HKCR\{ext}\shell", EntryScope.FileType, exts, includeBuiltIn);
                }
                using (var com = root.OpenSubKey($@"{ext}\shellex\ContextMenuHandlers"))
                {
                    if (com != null)
                        ReadComChildren(result, seen, com, $@"HKCR\{ext}\shellex\ContextMenuHandlers", EntryScope.FileType, exts);
                }
            }
        }
    }

    private static (string? name, string? module) ResolveClsid(string clsid)
    {
        if (string.IsNullOrWhiteSpace(clsid)) return (null, null);
        foreach (var view in Views)
        {
            using var root = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, view);
            using var key = root.OpenSubKey($@"CLSID\{clsid}");
            if (key == null) continue;
            string? name = key.GetValue(null) as string;
            using var inproc = key.OpenSubKey("InprocServer32");
            string? module = inproc?.GetValue(null) as string;
            if (!string.IsNullOrWhiteSpace(module)) module = ExpandPath(module);
            return (string.IsNullOrWhiteSpace(name) ? null : name.Trim(), module);
        }
        return (null, null);
    }

    private static string? AppFromClsid(string clsid)
    {
        var (name, module) = ResolveClsid(clsid);
        return name ?? AppFromModule(module);
    }

    private static string AppFromCommandLine(string command, string icon)
    {
        var app = AppFromModule(ExtractExe(command));
        if (app != null) return app;
        return AppFromModule(ExtractExe(icon)) ?? "Unknown";
    }

    private static string? AppFromModule(string? modulePath)
    {
        if (string.IsNullOrWhiteSpace(modulePath)) return null;
        var path = ExpandPath(modulePath);
        try
        {
            if (File.Exists(path))
            {
                var desc = FileVersionInfo.GetVersionInfo(path).FileDescription;
                if (!string.IsNullOrWhiteSpace(desc)) return desc.Trim();
            }
        }
        catch { /* ignore unreadable modules */ }
        var baseName = Path.GetFileNameWithoutExtension(path);
        return string.IsNullOrWhiteSpace(baseName) ? null : baseName;
    }

    private static string ExtractExe(string commandLine)
    {
        if (string.IsNullOrWhiteSpace(commandLine)) return "";
        var s = commandLine.Trim();
        string path;
        if (s.StartsWith('"'))
        {
            int end = s.IndexOf('"', 1);
            path = end > 0 ? s.Substring(1, end - 1) : s.Trim('"');
        }
        else
        {
            int sp = s.IndexOf(' ');
            path = sp > 0 ? s.Substring(0, sp) : s;
        }
        // Strip a trailing ",123" icon index if present.
        int comma = path.LastIndexOf(',');
        if (comma > 0 && int.TryParse(path[(comma + 1)..], out _))
            path = path[..comma];
        return path;
    }

    private static string CleanTitle(string value, string fallback)
    {
        if (!string.IsNullOrWhiteSpace(value) && !value.StartsWith('@'))
            return value.Trim();
        return Prettify(fallback);
    }

    private static string Prettify(string verb)
    {
        if (string.IsNullOrWhiteSpace(verb)) return "Unknown";
        var s = verb.Replace('_', ' ').Replace('-', ' ').Trim();
        if (s.Length == 0) return verb;
        return char.ToUpperInvariant(s[0]) + s[1..];
    }

    private static string ExpandPath(string? path)
    {
        if (string.IsNullOrWhiteSpace(path)) return "";
        path = path.Trim().Trim('"');
        try { return Environment.ExpandEnvironmentVariables(path); } catch { return path; }
    }

    /// <summary>Reads the PE header of a module to decide 32-bit vs 64-bit.</summary>
    public static Bitness GetFileBitness(string? path)
    {
        if (string.IsNullOrWhiteSpace(path)) return Bitness.Unknown;
        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var br = new BinaryReader(fs);
            if (fs.Length < 0x40) return Bitness.Unknown;
            if (br.ReadUInt16() != 0x5A4D) return Bitness.Unknown; // "MZ"
            fs.Position = 0x3C;
            int peOffset = br.ReadInt32();
            if (peOffset <= 0 || peOffset + 6 > fs.Length) return Bitness.Unknown;
            fs.Position = peOffset;
            if (br.ReadUInt32() != 0x00004550) return Bitness.Unknown; // "PE\0\0"
            ushort machine = br.ReadUInt16();
            return machine switch
            {
                0x014c => Bitness.X86, // IMAGE_FILE_MACHINE_I386
                0x8664 => Bitness.X64, // IMAGE_FILE_MACHINE_AMD64
                0xAA64 => Bitness.X64, // IMAGE_FILE_MACHINE_ARM64
                0x01c4 => Bitness.X86, // IMAGE_FILE_MACHINE_ARMNT (32-bit)
                _ => Bitness.Unknown
            };
        }
        catch { return Bitness.Unknown; }
    }

    /// <summary>Merges duplicate COM handlers (same CLSID + scope across extensions) into one entry.</summary>
    private static List<LegacyEntry> MergeCom(List<LegacyEntry> entries)
    {
        var result = new List<LegacyEntry>();
        var byId = new Dictionary<string, LegacyEntry>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in entries)
        {
            if (e.Kind != EntryKind.Com)
            {
                result.Add(e);
                continue;
            }
            if (byId.TryGetValue(e.Id, out var existing))
            {
                foreach (var ext in e.Extensions)
                    if (!existing.Extensions.Contains(ext, StringComparer.OrdinalIgnoreCase))
                        existing.Extensions.Add(ext);
            }
            else
            {
                byId[e.Id] = e;
                result.Add(e);
            }
        }
        return result;
    }
}
