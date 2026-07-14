using RegistrationCodec.Client.Scenarios;
using RegistrationCodec.Client.Support;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var server = ZLinkHttpClient.Create(options.ServerUrl).Build();
using var codecRequester = ZLinkHttpClient.Create(options.CodecRequesterUrl).Build();

var scenarios = new (string Name, Func<Task> Run)[]
{
    ("RC-A1", () => RcA1AutoRegistrationScenario.RunAsync(server)),
    ("RC-A2", () => RcA2AttributeRegistrationScenario.RunAsync(server)),
    ("RC-A3", () => RcA3ManualRegistrationScenario.RunAsync(server)),
    ("RC-A4", () => RcA4DiLifecycleScenario.RunAsync(server)),
    ("RC-A5", () => RcA5FilterOrderingScenario.RunAsync(server)),
    ("RC-A6", () => RcA6InvalidRegistrationScenario.RunAsync(options)),
    ("RC-B1", () => RcB1JsonCodecScenario.RunAsync(server)),
    ("RC-B2", () => RcB2ProtobufCodecScenario.RunAsync(server)),
    ("RC-B3", () => RcB3MessagePackCodecScenario.RunAsync(server)),
    ("RC-B4", () => RcB4CodecCoexistenceScenario.RunAsync(server)),
    ("RC-B5", () => RcB5CodecMismatchScenario.RunAsync(codecRequester))
};

foreach (var name in SelectedScenarioNames(options.Scenario, scenarios.Select(scenario => scenario.Name)))
{
    var selected = scenarios.FirstOrDefault(scenario =>
        string.Equals(scenario.Name, name, StringComparison.OrdinalIgnoreCase));
    if (selected.Run is null) throw new ArgumentException($"Unknown scenario '{name}'.");

    await selected.Run();
}

Console.WriteLine("registration-codec e2e result=passed");

static IEnumerable<string> SelectedScenarioNames(string selector, IEnumerable<string> allNames)
{
    if (string.Equals(selector, "all", StringComparison.OrdinalIgnoreCase))
        return allNames;

    return selector
        .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
}
