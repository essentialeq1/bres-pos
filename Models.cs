namespace LegacyMenuBridge;

public enum EntryKind
{
    Command, // simple "shell\<verb>\command" entry we can run directly
    Com      // "shellex\ContextMenuHandlers" entry backed by a DLL
}

public enum EntryScope
{
    AllFiles,   // applies to every file        (HKCR\*)
    Folder,     // applies to folders           (HKCR\Directory)
    Background, // folder / desktop background  (HKCR\Directory\Background)
    Drive,      // applies to drives            (HKCR\Drive)
    FileType    // applies to one or more file extensions
}

public enum Bitness
{
    Unknown,
    X64,
    X86
}

public sealed class LegacyEntry
{
    // Stable id built from the registry location. Used by the import log so a
    // re-scan can skip entries that were already imported.
    public string Id { get; set; } = "";

    public string Title { get; set; } = "";

    // Grouping label shown in the UI, for example "7-Zip" or "Kaspersky".
    public string Application { get; set; } = "";

    public EntryKind Kind { get; set; }
    public EntryScope Scope { get; set; }

    // Set when the entry only applies to specific extensions (".mp4", ".mkv", ...).
    public List<string> Extensions { get; set; } = new();

    public string RegistryPath { get; set; } = "";

    // Command kind: the raw command line from the registry (may contain %1 etc).
    public string Command { get; set; } = "";

    // Optional icon reference, for example  "C:\App\app.exe",0
    public string Icon { get; set; } = "";

    // Com kind: the handler's CLSID, for example {AB12...}
    public string Clsid { get; set; } = "";

    // Com kind: optional canonical verb discovered during inspection.
    public string Verb { get; set; } = "";

    // Com kind: the DLL that backs the handler (from InprocServer32).
    public string ModulePath { get; set; } = "";

    public Bitness Bitness { get; set; } = Bitness.Unknown;

    // UI state.
    public bool IsNew { get; set; }
    public bool Selected { get; set; }

    public string Describe()
    {
        string kind = Kind == EntryKind.Com ? "COM" : "Command";
        string scope = Scope switch
        {
            EntryScope.AllFiles => "All files",
            EntryScope.Folder => "Folders",
            EntryScope.Background => "Background",
            EntryScope.Drive => "Drives",
            _ => Extensions.Count > 0 ? string.Join(" ", Extensions) : "File type"
        };
        return $"{kind} | {scope}";
    }
}
