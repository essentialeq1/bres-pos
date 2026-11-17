using System.Text;
using System.Text.Json;
using System.Windows.Forms;

namespace LegacyMenuBridge;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length >= 1)
        {
            switch (args[0])
            {
                case "--inspect": return RunInspect(args);
                case "--invoke": return RunInvoke(args);
                case "--verb": return RunVerb(args);
                case "--selftest": return Diagnostics.RunSelfTest();
            }
        }

        Application.SetHighDpiMode(HighDpiMode.SystemAware);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new MainForm());
        return 0;
    }

    // --inspect "<path>"  -> writes a JSON array of menu items to stdout.
    private static int RunInspect(string[] args)
    {
        if (args.Length < 2) return 2;
        try
        {
            var items = HandlerHost.Inspect(args[1]);
            Log.Write($"inspect path={args[1]} items={items.Count}");
            try
            {
                Console.OutputEncoding = Encoding.UTF8;
                Console.WriteLine(JsonSerializer.Serialize(items));
            }
            catch { }
            return 0;
        }
        catch (Exception ex)
        {
            Log.Write("inspect failed: " + ex.Message);
            return 1;
        }
    }

    // --invoke "<verb-or-title>" "<path>"
    private static int RunInvoke(string[] args)
    {
        if (args.Length < 3) return 2;
        try
        {
            bool ok = HandlerHost.Invoke(args[2], args[1]);
            Log.Write($"invoke selector={args[1]} path={args[2]} => {(ok ? "ok" : "failed")}");
            return ok ? 0 : 1;
        }
        catch (Exception ex)
        {
            Log.Write("invoke failed: " + ex.Message);
            return 1;
        }
    }

    // --verb "<verb>" "<path>"
    private static int RunVerb(string[] args)
    {
        if (args.Length < 3) return 2;
        try
        {
            bool ok = HandlerHost.InvokeShellVerb(args[2], args[1]);
            Log.Write($"verb={args[1]} path={args[2]} => {(ok ? "ok" : "failed")}");
            return ok ? 0 : 1;
        }
        catch (Exception ex)
        {
            Log.Write("verb failed: " + ex.Message);
            return 1;
        }
    }
}
