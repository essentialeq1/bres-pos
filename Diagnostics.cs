using System.Text;

namespace LegacyMenuBridge;

/// <summary>Tiny file logger so runs can be diagnosed from %LocalAppData%\LegacyMenuBridge\log.txt.</summary>
public static class Log
{
    private static readonly object Gate = new();

    private static string DirectoryPath
    {
        get
        {
            string dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "LegacyMenuBridge");
            Directory.CreateDirectory(dir);
            return dir;
        }
    }

    public static string LogFile => Path.Combine(DirectoryPath, "log.txt");

    public static void Write(string message)
    {
        try
        {
            lock (Gate)
            {
                using var fs = new FileStream(LogFile, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                using var sw = new StreamWriter(fs);
                sw.WriteLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {message}");
            }
        }
        catch { /* logging must never crash the app */ }
    }
}

/// <summary>Headless diagnostics, used by <c>--selftest</c> and the CI self-test step.</summary>
public static class Diagnostics
{
    public const string ProbeExt = ".lmbprobe";

    public static int RunSelfTest()
    {
        var sb = new StringBuilder();

        void Line(string s)
        {
            sb.AppendLine(s);
            try { Console.WriteLine(s); } catch { }
        }

        Line("=== LegacyMenuBridge self-test ===");
        Line("Time : " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
        Line("OS   : " + Environment.OSVersion.VersionString + " (build " + Environment.OSVersion.Version.Build + ")");
        Line("Bitness: OS " + (Environment.Is64BitOperatingSystem ? "x64" : "x86") + ", process " + (Environment.Is64BitProcess ? "x64" : "x86"));
        Line("Exe  : " + (Environment.ProcessPath ?? "?"));

        Line("");
        Line("[1/5] IKAS-MC detection");
        try
        {
            string? folder = IkasWriter.FindCustomCommandsFolder();
            Line(folder == null ? "  NOT FOUND (expected in CI)" : "  " + folder);
        }
        catch (Exception ex) { Line("  error: " + ex.Message); }

        Line("");
        Line("[2/5] Registry scan");
        try
        {
            var entries = RegistryScanner.Scan();
            Line($"  total: {entries.Count}   commands: {entries.Count(e => e.Kind == EntryKind.Command)}   com: {entries.Count(e => e.Kind == EntryKind.Com)}");
            Line($"  x64 handlers: {entries.Count(e => e.Bitness == Bitness.X64)}   x86 handlers: {entries.Count(e => e.Bitness == Bitness.X86)}");
            foreach (var e in entries.Take(12))
                Line($"    - [{e.Kind}] {e.Title}  ({e.Application})  {e.Describe()}");
        }
        catch (Exception ex) { Line("  error: " + ex.Message); }

        Line("");
        Line("[3/5] COM inspect (.txt sample)");
        int txtCount = InspectReport(sb, CreateSample(".txt"));

        Line("");
        Line("[4/5] COM inspect (.lmbprobe, no verbs expected)");
        InspectReport(sb, CreateSample(ProbeExt));

        Line("");
        string notepad = Path.Combine(Environment.SystemDirectory, "notepad.exe");
        Line("[5/5] COM inspect (notepad.exe — must have items)");
        int exeCount = InspectReport(sb, notepad);

        // Fold the trailing app log (which carries the COM HRESULT details) into this report.
        try
        {
            if (File.Exists(Log.LogFile))
            {
                Line("");
                Line("--- recent app log (log.txt) ---");
                foreach (var l in File.ReadAllLines(Log.LogFile).TakeLast(40))
                    Line(l);
            }
        }
        catch { }

        bool ok = exeCount > 0;
        Line("");
        Line(ok
            ? $"RESULT: OK - notepad.exe yielded {exeCount} items (.txt={txtCount}; COM interop works)."
            : "RESULT: FAIL - notepad.exe yielded no items; COM interop is not working on this machine.");

        string report = sb.ToString();
        try
        {
            File.WriteAllText(
                Path.Combine(Path.GetDirectoryName(Log.LogFile) ?? AppContext.BaseDirectory, "selftest.log"),
                report, new UTF8Encoding(false));
        }
        catch { }

        Log.Write($"self-test: {(ok ? "OK" : "FAIL")}");
        return ok ? 0 : 1;
    }

    private static int InspectReport(StringBuilder sb, string path)
    {
        try
        {
            var items = HandlerHost.Inspect(path);
            if (items.Count == 0)
            {
                sb.AppendLine("  no items returned for " + path);
                return 0;
            }
            sb.AppendLine($"  {items.Count} items for {path}:");
            foreach (var i in items)
                sb.AppendLine($"    - id={i.Id}  verb=\"{i.Verb}\"  title=\"{i.Title}\"");
            return items.Count;
        }
        catch (Exception ex)
        {
            sb.AppendLine("  error: " + ex.GetType().Name + ": " + ex.Message);
            return 0;
        }
    }

    private static string CreateSample(string ext)
    {
        if (!ext.StartsWith('.')) ext = "." + ext;
        string path = Path.Combine(Path.GetTempPath(), "lmb-selftest-" + Guid.NewGuid().ToString("N") + ext);
        try { File.WriteAllText(path, "LegacyMenuBridge self-test"); } catch { }
        return path;
    }
}
