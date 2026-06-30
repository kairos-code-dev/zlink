namespace YieldDispatch.Server.Delay;

internal sealed record NodeOptions(string Rid);

internal sealed class EvidenceStore
{
    private readonly List<string> _entries = [];
    private readonly string? _filePath;
    private readonly object _gate = new();

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        lock (_gate)
        {
            _entries.Add(entry);
            if (!string.IsNullOrWhiteSpace(_filePath)) File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot()
    {
        lock (_gate)
        {
            return _entries.ToArray();
        }
    }
}

internal sealed record DelayOptions(
    string Rid,
    string HttpUrl,
    string DelayEndpoint,
    string LogDir)
{
    public string EvidenceFile => Path.Combine(LogDir, $"{Rid}.evidence.log");

    public static DelayOptions Parse(string[] args)
    {
        var values = Cli.Parse(args);
        return new DelayOptions(
            Cli.Required(values, "rid"),
            Cli.Required(values, "http-url"),
            Cli.Required(values, "delay-endpoint"),
            Cli.Required(values, "log-dir"));
    }
}

internal static class Cli
{
    public static Dictionary<string, string> Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal)) continue;

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for {args[i]}.");

            values[args[i][2..]] = args[++i];
        }

        return values;
    }

    public static string Required(Dictionary<string, string> values, string key)
    {
        return values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
    }
}