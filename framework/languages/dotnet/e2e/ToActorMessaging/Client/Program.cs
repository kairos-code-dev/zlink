using ToActorMessaging.Shared;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
using var actorHttp = ZLinkHttpClient.Create(options.ActorUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var callerHttp = ZLinkHttpClient.Create(options.CallerUrl).Timeout(TimeSpan.FromSeconds(30)).Build();

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["TA-A1"] = async () =>
    {
        await PostAsync("/actors/ta-a1/ensure");
        await AssertCallAsync("TA-A1-send", "ta-a1", "a1-send", "sent", send: true);
        await AssertCallAsync("TA-A1-request", "ta-a1", "a1-request", "reply:a1-request", send: false);
    },
    ["TA-A2"] = async () =>
    {
        await PostAsync("/actors/ta-a2/ensure");
        await AssertCallAsync("TA-A2-unbound-send", "ta-a2", "a2-send", "sent", send: true);
        await AssertCallAsync("TA-A2-unbound-request", "ta-a2", "a2-request", "reply:a2-request", send: false);
    },
    ["TA-A3"] = async () =>
    {
        await AssertFailureAsync("TA-A3-before-bind", "ta-a3", "ActorRouteNotFound", send: true);
        await PostAsync("/actors/ta-a3/ensure");
        await AssertCallAsync("TA-A3-after-bind-send", "ta-a3", "a3-send", "sent", send: true);
        await AssertCallAsync("TA-A3-after-bind-request", "ta-a3", "a3-request", "reply:a3-request", send: false);
    },
    ["TA-A4"] = async () =>
    {
        await PostAsync("/actors/ta-a4/ensure");
        await AssertCallAsync("TA-A4-disconnected-send", "ta-a4", "a4-send", "sent", send: true);
        await AssertCallAsync("TA-A4-disconnected-request", "ta-a4", "a4-request", "reply:a4-request", send: false);
    },
    ["TA-B1"] = async () =>
    {
        await AssertFailureAsync("TA-B1-missing", "missing-actor", "ActorRouteNotFound", send: true);
        await AssertFailureAsync("TA-B1-missing-request", "missing-actor", "ActorRouteNotFound", send: false);
    }
};

foreach (var name in SelectedScenarioNames(options.Scenario, scenarios.Keys))
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");

    await scenario();
}

var evidence = (await actorHttp.Get("/evidence").SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
               ?? throw new InvalidOperationException("Actor evidence endpoint returned null.");
RequireEvidenceIfSelected("TA-A1", "TA-A1-send", "send", "TA-A1 send evidence missing.");
RequireEvidenceIfSelected("TA-A1", "TA-A1-request", "request", "TA-A1 request evidence missing.");
RequireEvidenceIfSelected("TA-A2", "TA-A2-unbound-send", "send", "TA-A2 send evidence missing.");
RequireEvidenceIfSelected("TA-A3", "TA-A3-after-bind-request", "request", "TA-A3 request evidence missing.");
RequireEvidenceIfSelected("TA-A4", "TA-A4-disconnected-send", "send", "TA-A4 send evidence missing.");

Console.WriteLine("to-actor-messaging e2e result=passed");

void RequireEvidenceIfSelected(string scenario, string evidenceScenario, string kind, string message)
{
    if (!ScenarioSelected(options.Scenario, scenario, scenarios.Keys))
        return;

    Require(evidence.Any(item => item.Scenario == evidenceScenario && item.Kind == kind), message);
}

async Task AssertCallAsync(string scenario, string actorId, string value, string expected, bool send)
{
    var endpoint = send ? "send" : "request";
    var response = await PostJsonAsync<ActorCallResponse>(
        $"/{endpoint}",
        new ActorCallRequest(scenario, actorId, value));
    Require(response.Result == expected, $"{scenario} expected '{expected}', got '{response.Result}'.");
    Require(response.ErrorKind is null, $"{scenario} unexpected error '{response.ErrorKind}'.");
}

async Task AssertFailureAsync(string scenario, string actorId, string expectedKind, bool send)
{
    var endpoint = send ? "send" : "request";
    var response = await PostJsonAsync<ActorCallResponse>(
        $"/{endpoint}",
        new ActorCallRequest(scenario, actorId, "missing"));
    Require(response.ErrorKind == expectedKind, $"{scenario} expected '{expectedKind}', got '{response.ErrorKind}'.");
}

async Task PostAsync(string path)
{
    await actorHttp.Post(path).Body(new { }).SubmitAsync<object>();
}

async Task<T> PostJsonAsync<T>(string path, object body)
{
    return (await callerHttp.Post(path).Body(body).SubmitAsync<T>()).Body
           ?? throw new InvalidOperationException($"Endpoint '{path}' returned null.");
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static IEnumerable<string> SelectedScenarioNames(string selector, IEnumerable<string> allNames)
{
    if (string.Equals(selector, "all", StringComparison.OrdinalIgnoreCase))
        return allNames;

    return selector
        .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
}

static bool ScenarioSelected(string selector, string scenario, IEnumerable<string> allNames)
{
    return SelectedScenarioNames(selector, allNames)
        .Any(name => string.Equals(name, scenario, StringComparison.OrdinalIgnoreCase));
}

internal sealed record ClientOptions(string ActorUrl, string CallerUrl, string Scenario)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            var key = args[i].TrimStart('-');
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[key] = args[i + 1];
        }

        return new ClientOptions(
            values["actor-url"],
            values["caller-url"],
            values.TryGetValue("scenario", out var scenario) ? scenario : "all");
    }
}
