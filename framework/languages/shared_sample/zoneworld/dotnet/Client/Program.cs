using ZoneWorld.Client;

var requested = args.FirstOrDefault() ?? "all";
var options = ClientOptions.Create();

// Each scenario gets its own budget. A shared one would make a scenario fail because the ones
// before it took their time waiting for things the world is allowed to take time over — a
// timing contract the canonical never states.
var scenarioBudget = TimeSpan.FromMinutes(2);

var selected = requested.Equals("all", StringComparison.OrdinalIgnoreCase)
    ? Scenarios.All.Keys.ToArray()
    : requested.Split(',', StringSplitOptions.RemoveEmptyEntries);

var failures = new List<string>();

foreach (var id in selected)
{
    if (!Scenarios.All.TryGetValue(id, out var scenario)
        && !Scenarios.RunnerDriven.TryGetValue(id, out scenario))
    {
        Console.Error.WriteLine($"unknown scenario: {id}");
        return 2;
    }

    using var cancellation = new CancellationTokenSource(scenarioBudget);
    try
    {
        await scenario(options, cancellation.Token);
        Console.WriteLine($"scenario {id} passed");
    }
    catch (Exception error)
    {
        failures.Add(id);
        Console.Error.WriteLine($"scenario {id} FAILED: {error.Message}");
    }
}

if (failures.Count > 0)
{
    Console.WriteLine($"zoneworld result=failed failures={string.Join(',', failures)}");
    return 1;
}

Console.WriteLine("zoneworld=completed");
return 0;
