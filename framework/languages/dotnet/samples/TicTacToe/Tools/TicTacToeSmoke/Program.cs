using TicTacToe.Direct;
using TicTacToe.SessionGateway;

var mode = ReadOption(args, "--mode") ?? "direct";
var logPath = ReadOption(args, "--log");
IReadOnlyList<string> lines;

switch (mode)
{
    case "direct":
    {
        var result = await new DirectSampleScenario().RunAsync();
        result.AssertPassed();
        lines = result.LogLines;
        Console.WriteLine($"direct smoke passed: match={result.MatchId}, board={result.FinalState.Board}");
        break;
    }
    case "session-gateway":
    {
        var result = await new SessionGatewaySampleScenario().RunAsync();
        result.AssertPassed();
        lines = result.LogLines;
        Console.WriteLine($"session-gateway smoke passed: match={result.MatchId}, board={result.FinalState.Board}");
        break;
    }
    default:
        throw new ArgumentException($"Unknown --mode '{mode}'. Expected 'direct' or 'session-gateway'.");
}

if (!string.IsNullOrWhiteSpace(logPath))
{
    var directory = Path.GetDirectoryName(Path.GetFullPath(logPath));
    if (!string.IsNullOrEmpty(directory))
    {
        Directory.CreateDirectory(directory);
    }

    await File.WriteAllLinesAsync(logPath, lines);
}

foreach (var line in lines)
{
    Console.WriteLine(line);
}

static string? ReadOption(string[] args, string name)
{
    for (var i = 0; i < args.Length; i++)
    {
        if (args[i] == name && i + 1 < args.Length)
        {
            return args[i + 1];
        }
    }

    return null;
}
