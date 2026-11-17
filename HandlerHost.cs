using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace LegacyMenuBridge;

/// <summary>A single item discovered from a legacy (classic) context menu.</summary>
public sealed class MenuItemInfo
{
    public int Id { get; set; }
    public string Title { get; set; } = "";
    public string Verb { get; set; } = "";
}

/// <summary>
/// Loads the classic (merged) shell context menu for a file/folder and either
/// lists its items or invokes one. The menu is fetched the canonical way: parse
/// the path into a PIDL, bind to the parent folder, then GetUIObjectOf(IContextMenu).
/// </summary>
public static class HandlerHost
{
    private static readonly Guid IID_IShellFolder = new("000214E6-0000-0000-C000-000000000046");
    private static readonly Guid IID_IContextMenu = new("000214E4-0000-0000-C000-000000000046");

    private const uint CMF_NORMAL = 0x00000000;
    private const uint GCS_VERBW = 0x00000004;
    private const uint GCS_VERBA = 0x00000000;
    private const uint CMIC_MASK_UNICODE = 0x00004000;
    private const uint CMIC_MASK_ASYNCOK = 0x00100000;
    private const int SW_SHOWNORMAL = 1;

    /// <summary>Runs inspection in a child process so a bad handler DLL can only crash the child.</summary>
    public static List<MenuItemInfo> InspectIsolated(string path) => InspectIsolated(path, Bitness.Unknown);

    /// <summary>Same as above but chooses the helper matching the handler bitness.</summary>
    public static List<MenuItemInfo> InspectIsolated(string path, Bitness bitness)
    {
        try
        {
            string self = Paths.HelperFor(bitness);
            if (string.IsNullOrEmpty(self)) return Inspect(path);

            var psi = new ProcessStartInfo
            {
                FileName = self,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            psi.ArgumentList.Add("--inspect");
            psi.ArgumentList.Add(path);

            using var p = Process.Start(psi);
            if (p == null) return Inspect(path);
            string json = p.StandardOutput.ReadToEnd();
            p.WaitForExit(15000);
            if (string.IsNullOrWhiteSpace(json)) return new List<MenuItemInfo>();
            return JsonSerializer.Deserialize<List<MenuItemInfo>>(json) ?? new List<MenuItemInfo>();
        }
        catch
        {
            return Inspect(path);
        }
    }

    /// <summary>Lists every item of the classic menu that would appear for <paramref name="path"/>.</summary>
    public static List<MenuItemInfo> Inspect(string path)
    {
        var items = new List<MenuItemInfo>();
        IntPtr itemPtr = IntPtr.Zero, cmPtr = IntPtr.Zero, hMenu = IntPtr.Zero;
        IContextMenu? cm = null;
        try
        {
            if (!TryCreateContextMenu(path, out itemPtr, out cmPtr))
            {
                Log.Write($"inspect {Path.GetFileName(path)}: create failed");
                return items;
            }
            cm = (IContextMenu)Marshal.GetObjectForIUnknown(cmPtr);
            hMenu = Native.CreatePopupMenu();
            int qhr = cm.QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
            Log.Write($"inspect {Path.GetFileName(path)}: QueryContextMenu hr=0x{qhr:X8}");
            if (qhr < 0) return items;

            int count = Native.GetMenuItemCount(hMenu);
            Log.Write($"inspect {Path.GetFileName(path)}: menu count={count}");
            for (int i = 0; i < count; i++)
            {
                uint id = Native.GetMenuItemID(hMenu, i);
                if (id == 0 || id == 0xFFFFFFFF) continue; // separator or submenu
                string title = Native.GetMenuString(hMenu, i);
                string verb = GetVerb(cm, id) ?? "";
                if (string.IsNullOrEmpty(title) && string.IsNullOrEmpty(verb)) continue;
                items.Add(new MenuItemInfo { Id = (int)id, Title = title, Verb = verb });
            }
        }
        catch (Exception ex)
        {
            Log.Write($"inspect {Path.GetFileName(path)}: threw {ex.GetType().Name}: {ex.Message}");
        }
        finally
        {
            if (hMenu != IntPtr.Zero) Native.DestroyMenu(hMenu);
            if (cm != null) { try { Marshal.ReleaseComObject(cm); } catch { } }
            Release(itemPtr, cmPtr);
        }
        return items;
    }

    /// <summary>Rebuilds the classic menu for <paramref name="path"/> and fires the matching item.</summary>
    public static bool Invoke(string path, string selector)
    {
        IntPtr itemPtr = IntPtr.Zero, cmPtr = IntPtr.Zero, hMenu = IntPtr.Zero;
        IContextMenu? cm = null;
        try
        {
            if (!TryCreateContextMenu(path, out itemPtr, out cmPtr)) return false;
            cm = (IContextMenu)Marshal.GetObjectForIUnknown(cmPtr);
            hMenu = Native.CreatePopupMenu();
            if (cm.QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL) < 0) return false;

            int count = Native.GetMenuItemCount(hMenu);
            int targetId = -1;
            string verb = "";
            for (int i = 0; i < count; i++)
            {
                uint id = Native.GetMenuItemID(hMenu, i);
                if (id == 0 || id == 0xFFFFFFFF) continue;
                string title = Native.GetMenuString(hMenu, i);
                string v = GetVerb(cm, id) ?? "";
                if (string.Equals(v, selector, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(title, selector, StringComparison.OrdinalIgnoreCase))
                {
                    targetId = (int)id; verb = v; break;
                }
            }
            if (targetId < 0) return false;
            return InvokeById(cm, targetId, verb);
        }
        catch { return false; }
        finally
        {
            if (hMenu != IntPtr.Zero) Native.DestroyMenu(hMenu);
            if (cm != null) { try { Marshal.ReleaseComObject(cm); } catch { } }
            Release(itemPtr, cmPtr);
        }
    }

    /// <summary>Fires a plain shell verb through the shell (covers DelegateExecute entries).</summary>
    public static bool InvokeShellVerb(string path, string verb)
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = path, Verb = verb, UseShellExecute = true, WindowStyle = ProcessWindowStyle.Normal
            };
            Process.Start(psi);
            return true;
        }
        catch { return false; }
    }

    // -------------------- internals --------------------

    private static bool TryCreateContextMenu(string path, out IntPtr itemPtr, out IntPtr cmPtr)
    {
        itemPtr = IntPtr.Zero;
        cmPtr = IntPtr.Zero;
        IntPtr pidl = IntPtr.Zero, parentPtr = IntPtr.Zero, childPidl = IntPtr.Zero;
        IShellFolder? parent = null;
        try
        {
            int hr = Native.SHParseDisplayName(path, IntPtr.Zero, out pidl, 0, out _);
            Log.Write($"inspect {Path.GetFileName(path)}: SHParseDisplayName hr=0x{hr:X8} pidl={(pidl != IntPtr.Zero)}");
            if (hr != 0 || pidl == IntPtr.Zero) return false;

            Guid iidFolder = IID_IShellFolder;
            hr = Native.SHBindToParent(pidl, ref iidFolder, out parentPtr, out childPidl);
            Log.Write($"inspect {Path.GetFileName(path)}: SHBindToParent hr=0x{hr:X8} child={(childPidl != IntPtr.Zero)}");
            if (hr != 0 || parentPtr == IntPtr.Zero || childPidl == IntPtr.Zero) return false;

            parent = (IShellFolder)Marshal.GetObjectForIUnknown(parentPtr);
            Guid iidCm = IID_IContextMenu;
            var apidl = new[] { childPidl };
            hr = parent.GetUIObjectOf(IntPtr.Zero, 1, apidl, ref iidCm, IntPtr.Zero, out cmPtr);
            Log.Write($"inspect {Path.GetFileName(path)}: GetUIObjectOf hr=0x{hr:X8} cm={(cmPtr != IntPtr.Zero)}");
            return hr == 0 && cmPtr != IntPtr.Zero;
        }
        catch (Exception ex)
        {
            Log.Write($"inspect {Path.GetFileName(path)}: create threw {ex.GetType().Name}: {ex.Message}");
            return false;
        }
        finally
        {
            if (parent != null) { try { Marshal.ReleaseComObject(parent); } catch { } }
            if (parentPtr != IntPtr.Zero) { try { Marshal.Release(parentPtr); } catch { } }
            if (pidl != IntPtr.Zero) { Native.ILFree(pidl); }
        }
    }

    private static bool InvokeById(IContextMenu cm, int id, string verb)
    {
        var info = new CMINVOKECOMMANDINFOEX
        {
            cbSize = Marshal.SizeOf<CMINVOKECOMMANDINFOEX>(),
            nShow = SW_SHOWNORMAL,
            fMask = CMIC_MASK_UNICODE | CMIC_MASK_ASYNCOK
        };

        IntPtr verbPtr = IntPtr.Zero;
        try
        {
            if (!string.IsNullOrEmpty(verb))
            {
                verbPtr = Marshal.StringToCoTaskMemUni(verb);
                info.lpVerbW = verbPtr;
            }
            else
            {
                info.lpVerb = new IntPtr(id); // MAKEINTRESOURCE(id)
            }
            int hr = cm.InvokeCommand(ref info);
            return hr >= 0;
        }
        finally
        {
            if (verbPtr != IntPtr.Zero) Marshal.FreeCoTaskMem(verbPtr);
        }
    }

    private static string? GetVerb(IContextMenu cm, uint id)
    {
        IntPtr buf = Marshal.AllocHGlobal(1040);
        try
        {
            Zero(buf, 1040);
            if (cm.GetCommandString(new UIntPtr(id), GCS_VERBW, IntPtr.Zero, buf, 520) == 0)
            {
                string? s = Marshal.PtrToStringUni(buf);
                if (!string.IsNullOrEmpty(s)) return s;
            }
            Zero(buf, 1040);
            if (cm.GetCommandString(new UIntPtr(id), GCS_VERBA, IntPtr.Zero, buf, 520) == 0)
            {
                string? s = Marshal.PtrToStringAnsi(buf);
                if (!string.IsNullOrEmpty(s)) return s;
            }
        }
        catch { }
        finally { Marshal.FreeHGlobal(buf); }
        return null;
    }

    private static void Zero(IntPtr buf, int bytes)
    {
        for (int i = 0; i < bytes; i++) Marshal.WriteByte(buf, i, 0);
    }

    private static void Release(IntPtr a, IntPtr b)
    {
        if (a != IntPtr.Zero) { try { Marshal.Release(a); } catch { } }
        if (b != IntPtr.Zero) { try { Marshal.Release(b); } catch { } }
    }

    // -------------------- COM interop --------------------

    [ComImport, Guid("000214E6-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IShellFolder
    {
        void ParseDisplayName();
        void EnumObjects();
        void BindToObject();
        void BindToStorage();
        void CompareIDs();
        void CreateViewObject();
        void GetAttributesOf();
        [PreserveSig] int GetUIObjectOf(IntPtr hwndOwner, uint cidl,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] IntPtr[] apidl,
            ref Guid riid, IntPtr rgfReserved, out IntPtr ppv);
    }

    [ComImport, Guid("000214E4-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IContextMenu
    {
        [PreserveSig] int QueryContextMenu(IntPtr hmenu, uint indexMenu, uint idCmdFirst, uint idCmdLast, uint uFlags);
        [PreserveSig] int InvokeCommand(ref CMINVOKECOMMANDINFOEX pici);
        [PreserveSig] int GetCommandString(UIntPtr uCmd, uint uType, IntPtr pReserved, IntPtr pszName, uint cchMax);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct CMINVOKECOMMANDINFOEX
    {
        public int cbSize;
        public uint fMask;
        public IntPtr hwnd;
        public IntPtr lpVerb;
        public IntPtr lpParameters;
        public IntPtr lpDirectory;
        public int nShow;
        public uint dwHotKey;
        public IntPtr hIcon;
        public IntPtr lpTitle;
        public IntPtr lpVerbW;
        public IntPtr lpParametersW;
        public IntPtr lpDirectoryW;
        public IntPtr lpTitleW;
        public int ptX;
        public int ptY;
    }

    private static class Native
    {
        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        public static extern int SHParseDisplayName(
            [MarshalAs(UnmanagedType.LPWStr)] string pszName, IntPtr pbc, out IntPtr ppidl, uint sfgaoIn, out uint psfgaoOut);

        [DllImport("shell32.dll")]
        public static extern int SHBindToParent(IntPtr pidl, ref Guid riid, out IntPtr ppv, out IntPtr ppidlLast);

        [DllImport("shell32.dll")]
        public static extern void ILFree(IntPtr pidl);

        [DllImport("user32.dll")]
        public static extern IntPtr CreatePopupMenu();

        [DllImport("user32.dll")]
        public static extern bool DestroyMenu(IntPtr hMenu);

        [DllImport("user32.dll")]
        public static extern int GetMenuItemCount(IntPtr hMenu);

        [DllImport("user32.dll")]
        public static extern uint GetMenuItemID(IntPtr hMenu, int nPos);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetMenuStringW")]
        private static extern int GetMenuStringNative(IntPtr hMenu, uint uIDItem, StringBuilder lpString, int nMaxCount, uint uFlag);

        public static string GetMenuString(IntPtr hMenu, int position)
        {
            var sb = new StringBuilder(512);
            GetMenuStringNative(hMenu, (uint)position, sb, sb.Capacity, 0x00000400);
            return sb.ToString();
        }
    }
}
