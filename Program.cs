using System.Text;
using System.Text.Json;
using System.Windows.Forms;

namespace LegacyMenuBridge;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        // Hidden modes used by the JSON entries we generate for IKAS-MC.
        if (args.Length >= 1)
        {
            switch (args[0])
            {
                case "--inspect": return RunInspect(args);
                case "--invoke": return RunInvoke(args);
                case "--verb": return RunVerb(args);
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
            try
            {
                Console.OutputEncoding = Encoding.UTF8;
                Console.WriteLine(JsonSerializer.Serialize(items));
            }
            catch { }
            return 0;
        }
        catch { return 1; }
    }

    // --invoke "<verb-or-title>" "<path>"
    private static int RunInvoke(string[] args)
    {
        if (args.Length < 3) return 2;
        try { return HandlerHost.Invoke(args[2], args[1]) ? 0 : 1; }
        catch { return 1; }
    }

    // --verb "<verb>" "<path>"
    private static int RunVerb(string[] args)
    {
        if (args.Length < 3) return 2;
        try { return HandlerHost.InvokeShellVerb(args[2], args[1]) ? 0 : 1; }
        catch { return 1; }
    }
}
