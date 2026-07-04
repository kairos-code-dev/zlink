using System.Net.Http.Json;
using ToActorMessaging.Shared;

var options = ClientOptions.Parse(args);
using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };

await PostAsync($"{options.ActorUrl}/actors/ta-a1/ensure", null);
await AssertCallAsync("TA-A1-send", "ta-a1", "a1-send", "sent", send: true);
await AssertCallAsync("TA-A1-request", "ta-a1", "a1-request", "reply:a1-request", send: false);

await PostAsync($"{options.ActorUrl}/actors/ta-a2/ensure", null);
await AssertCallAsync("TA-A2-unbound-send", "ta-a2", "a2-send", "sent", send: true);
await AssertCallAsync("TA-A2-unbound-request", "ta-a2", "a2-request", "reply:a2-request", send: false);

await AssertFailureAsync("TA-A3-before-bind", "ta-a3", "ActorRouteNotFound", send: true);
await PostAsync($"{options.ActorUrl}/actors/ta-a3/ensure", null);
await AssertCallAsync("TA-A3-after-bind-send", "ta-a3", "a3-send", "sent", send: true);
await AssertCallAsync("TA-A3-after-bind-request", "ta-a3", "a3-request", "reply:a3-request", send: false);

await PostAsync($"{options.ActorUrl}/actors/ta-a4/ensure", null);
await AssertCallAsync("TA-A4-disconnected-send", "ta-a4", "a4-send", "sent", send: true);
await AssertCallAsync("TA-A4-disconnected-request", "ta-a4", "a4-request", "reply:a4-request", send: false);

await AssertFailureAsync("TA-B1-missing", "missing-actor", "ActorRouteNotFound", send: true);
await AssertFailureAsync("TA-B1-missing-request", "missing-actor", "ActorRouteNotFound", send: false);

var evidence = await http.GetFromJsonAsync<IReadOnlyList<ActorEvidence>>($"{options.ActorUrl}/evidence")
               ?? throw new InvalidOperationException("Actor evidence endpoint returned null.");
Require(evidence.Any(item => item.Scenario == "TA-A1-send" && item.Kind == "send"), "TA-A1 send evidence missing.");
Require(evidence.Any(item => item.Scenario == "TA-A1-request" && item.Kind == "request"), "TA-A1 request evidence missing.");
Require(evidence.Any(item => item.Scenario == "TA-A2-unbound-send" && item.Kind == "send"), "TA-A2 send evidence missing.");
Require(evidence.Any(item => item.Scenario == "TA-A3-after-bind-request" && item.Kind == "request"), "TA-A3 request evidence missing.");
Require(evidence.Any(item => item.Scenario == "TA-A4-disconnected-send" && item.Kind == "send"), "TA-A4 send evidence missing.");

Console.WriteLine("to-actor-messaging e2e result=passed");

async Task AssertCallAsync(string scenario, string actorId, string value, string expected, bool send)
{
    var endpoint = send ? "send" : "request";
    var response = await PostJsonAsync<ActorCallResponse>(
        $"{options.CallerUrl}/{endpoint}",
        new ActorCallRequest(scenario, actorId, value));
    Require(response.Result == expected, $"{scenario} expected '{expected}', got '{response.Result}'.");
    Require(response.ErrorKind is null, $"{scenario} unexpected error '{response.ErrorKind}'.");
}

async Task AssertFailureAsync(string scenario, string actorId, string expectedKind, bool send)
{
    var endpoint = send ? "send" : "request";
    var response = await PostJsonAsync<ActorCallResponse>(
        $"{options.CallerUrl}/{endpoint}",
        new ActorCallRequest(scenario, actorId, "missing"));
    Require(response.ErrorKind == expectedKind, $"{scenario} expected '{expectedKind}', got '{response.ErrorKind}'.");
}

async Task PostAsync(string url, object? body)
{
    using var response = await http.PostAsJsonAsync(url, body ?? new { });
    response.EnsureSuccessStatusCode();
}

async Task<T> PostJsonAsync<T>(string url, object body)
{
    using var response = await http.PostAsJsonAsync(url, body);
    response.EnsureSuccessStatusCode();
    return await response.Content.ReadFromJsonAsync<T>()
           ?? throw new InvalidOperationException($"Endpoint '{url}' returned null.");
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

internal sealed record ClientOptions(string ActorUrl, string CallerUrl)
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
            values["caller-url"]);
    }
}
