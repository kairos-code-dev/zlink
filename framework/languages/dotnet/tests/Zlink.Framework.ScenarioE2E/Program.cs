using System.Text.Json;
using Zlink.Framework.ScenarioE2E;

// Thin entry point, like the samples' Program.Main: it only routes a role to the
// shared server, a client scenario, or a topology query. All behaviour lives in
// ScenarioServer and the chapter scenario files.
if (args.Length == 0)
{
    throw new InvalidOperationException("scenario E2E role is required (server | client | topology).");
}

var role = args[0];
var rest = args.Skip(1).ToArray();

switch (role)
{
    case "server":
        await RunServerAsync(ScenarioArgs.Parse(rest));
        break;
    case "client":
        await RunClientAsync(ScenarioArgs.Parse(rest));
        break;
    case "topology":
        Console.WriteLine(ScenarioCatalog.DescribeTopology(rest[0]));
        break;
    case "list":
        Console.WriteLine(string.Join('\n', ScenarioCatalog.Ids));
        break;
    default:
        throw new InvalidOperationException($"Unknown scenario E2E role '{role}'.");
}

static async Task RunServerAsync(ScenarioArgs args)
{
    await ScenarioServer.RunAsync(new ScenarioServerOptions(
        WorkDir: args.Required("work-dir"),
        ServerName: args.Value("server-name", "api-a"),
        Mode: args.Value("mode", "channel"),
        Channel: args.Value("channel", "scenario-api"),
        ZLinkEndpoint: args.Required("zlink-endpoint"),
        HttpEndpoint: args.Required("http-endpoint"),
        ReadyFile: args.Required("ready-file"),
        RoutePeerEndpoints: args.List("route-peer-endpoints")));
}

static async Task RunClientAsync(ScenarioArgs args)
{
    var id = args.Required("scenario");
    var entry = ScenarioCatalog.Resolve(id);
    var serverNames = args.List("server-names");

    var context = new ScenarioContext(
        Channel: args.Value("channel", "scenario-api"),
        ServerNames: serverNames,
        ZLinkEndpoints: args.List("zlink-endpoint"),
        HttpEndpoints: args.List("http-endpoint"),
        ClientRoutingId: args.Value("client-routing-id", "peer-a"),
        ClientZLinkEndpoint: args.Value("client-zlink-endpoint", ""),
        RoutePeerEndpoints: args.List("route-peer-endpoints"));

    await entry.Run(context);

    var report = new ScenarioReport(
        ScenarioId: id,
        Language: "dotnet",
        Result: "passed",
        Passed: 1,
        Failed: 0,
        ProcessCount: serverNames.Count + 1,
        LogPath: args.Value("log-dir", ""),
        CompletedAt: DateTimeOffset.UtcNow);

    var reportFile = args.Value("report-file", "");
    if (!string.IsNullOrWhiteSpace(reportFile))
    {
        await File.WriteAllTextAsync(
            reportFile,
            JsonSerializer.Serialize(report, new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = true }));
    }

    Console.WriteLine($"scenario-e2e result=passed scenario={id} language=dotnet");
}

internal sealed record ScenarioReport(
    string ScenarioId,
    string Language,
    string Result,
    int Passed,
    int Failed,
    int ProcessCount,
    string LogPath,
    DateTimeOffset CompletedAt);

// Minimal --name value argument reader shared by the roles.
internal sealed class ScenarioArgs
{
    private readonly IReadOnlyDictionary<string, string> _values;

    private ScenarioArgs(IReadOnlyDictionary<string, string> values) => _values = values;

    public static ScenarioArgs Parse(IReadOnlyList<string> args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 0; index < args.Count; index += 2)
        {
            if (index + 1 >= args.Count || !args[index].StartsWith("--", StringComparison.Ordinal))
            {
                throw new InvalidOperationException("scenario E2E arguments must use --name value pairs.");
            }

            values[args[index][2..]] = args[index + 1];
        }

        return new ScenarioArgs(values);
    }

    public string Value(string name, string fallback)
        => _values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value) ? value : fallback;

    public string Required(string name)
        => _values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new InvalidOperationException($"--{name} is required.");

    public IReadOnlyList<string> List(string name)
        => Value(name, "").Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
}
