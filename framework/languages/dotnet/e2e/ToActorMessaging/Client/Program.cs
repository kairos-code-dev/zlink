using Systems.Zlink.Stream.Connector.Contracts;
using ToActorMessaging.Shared;
using Zlink.HttpClient;
using Zlink.Framework.Contracts.Actors;

var options = ClientOptions.Parse(args);
using var actorHttp = ZLinkHttpClient.Create(options.ActorUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var actorBHttp = ZLinkHttpClient.Create(options.ActorBUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var callerHttp = ZLinkHttpClient.Create(options.CallerUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var sessionAHttp = ZLinkHttpClient.Create(options.SessionAUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var sessionBHttp = ZLinkHttpClient.Create(options.SessionBUrl).Timeout(TimeSpan.FromSeconds(30)).Build();

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["TA-A1"] = async () =>
    {
        const string actorId = "ta-a1";
        await PostAsync($"/actors/{actorId}/ensure");
        await using var bound = await ConnectAndBindAsync(options.SessionAStreamEndpoint, actorId);
        await using var unbound = await ConnectAsync(options.SessionBStreamEndpoint);
        await AssertBoundPushAsync(bound, unbound, "TA-A1", actorId, "BeforeNotify");
        await AssertCallAsync("TA-A1-send", actorId, "a1-send", "sent", send: true);
        await AssertCallAsync("TA-A1-request", actorId, "a1-request", "reply:a1-request", send: false);
        await AssertBoundPushAsync(bound, unbound, "TA-A1", actorId, "AfterNotify");
    },
    ["TA-A2"] = async () =>
    {
        await actorBHttp.Post("/actors/ta-a2/ensure").Body(new { }).SubmitAsync<object>();
        await AssertCallAsync("TA-A2-unbound-send", "ta-a2", "a2-send", "sent", send: true);
        await AssertCallAsync("TA-A2-unbound-request", "ta-a2", "a2-request", "reply:a2-request", send: false);
        await AssertBoundPushFailureAsync(actorBHttp, "TA-A2", "ta-a2", "must-remain-unbound");
    },
    ["TA-A3"] = async () =>
    {
        const string actorId = "ta-a3";
        await PostAsync($"/actors/{actorId}/ensure");
        await AssertCallAsync("TA-A3-before-bind-send", actorId, "a3-before-send", "sent", send: true);
        await AssertCallAsync(
            "TA-A3-before-bind-request",
            actorId,
            "a3-before-request",
            "reply:a3-before-request",
            send: false);
        await AssertBoundPushFailureAsync(actorHttp, "TA-A3-before-bind", actorId, "must-remain-unbound");
        await using var bound = await ConnectAndBindAsync(options.SessionBStreamEndpoint, actorId);
        await AssertCallAsync("TA-A3-after-bind-send", actorId, "a3-after-send", "sent", send: true);
        await AssertCallAsync(
            "TA-A3-after-bind-request",
            actorId,
            "a3-after-request",
            "reply:a3-after-request",
            send: false);
        await AssertBoundPushAsync(bound, null, "TA-A3", actorId, "LateBindNotify");
    },
    ["TA-A4"] = async () =>
    {
        const string actorId = "ta-a4";
        await PostAsync($"/actors/{actorId}/ensure");
        await CaptureAsync(actorId);
        await using (var bound = await ConnectAndBindAsync(options.SessionAStreamEndpoint, actorId))
        {
            await AssertBoundPushAsync(bound, null, "TA-A4", actorId, "BeforeDisconnect");
            await bound.Close.Async();
        }
        await WaitForSessionEvidenceAsync(
            sessionAHttp,
            "session-disconnected|",
            "TA-A4 session disconnect evidence missing.");
        await AssertBoundPushFailureAsync(actorHttp, "TA-A4", actorId, "AfterDisconnect");
        await AssertCallWithRetryAsync(
            "TA-A4-disconnected-request",
            actorId,
            "a4-request",
            "reply:a4-request");
        await AssertCallAsync("TA-A4-disconnected-send", actorId, "a4-send", "sent", send: true);
        await actorHttp.Post($"/actors/{actorId}/destroy?scenario=TA-A4")
            .Body(new { })
            .SubmitAsync<DestroyActorReply>();
        await AssertCachedFailureWithRetryAsync(
            "TA-A4-destroyed-request",
            actorId,
            "ActorRouteNotFound");
    },
    ["TA-B1"] = async () =>
    {
        await AssertRouteAbsentAsync("missing-actor");
        await AssertCallAsync("TA-B1-missing", "missing-actor", "missing", "sent", send: true);
        await AssertFailureAsync("TA-B1-missing-request", "missing-actor", "ActorRouteNotFound", send: false);
        await AssertNoActorEvidenceAsync("missing-actor");
        await AssertRouteAbsentAsync("missing-actor");
    },
    ["TA-B2"] = async () =>
    {
        const string actorId = "ta-b2";
        await PostWithRetryAsync($"/actors/{actorId}/ensure");
        var staleRef = await CaptureAsync(actorId);
        await actorHttp.Post($"/actors/{actorId}/destroy?scenario=TA-B2")
            .Body(new { })
            .SubmitAsync<DestroyActorReply>();
        await PostWithRetryAsync($"/actors/{actorId}/ensure");

        await AssertCachedFailureAsync(
            "TA-B2-stale-request",
            actorId,
            "ActorLocationStale");
        var afterStale = await GetAllActorEvidenceAsync();
        Require(
            afterStale.All(item => item.Scenario != "TA-B2-stale-request"),
            "TA-B2 stale generation unexpectedly reached an actor handler.");
        var freshRef = await CaptureAsync(actorId);
        Require(
            freshRef.NodeRid == staleRef.NodeRid && freshRef.Generation > staleRef.Generation,
            $"TA-B2 expected a newer generation on the same owner. stale={staleRef}, fresh={freshRef}");
        await AssertCallWithRetryAsync(
            "TA-B2-fresh-request",
            actorId,
            "b2-fresh",
            "reply:b2-fresh");
        var afterFresh = await GetAllActorEvidenceAsync();
        Require(
            afterFresh.Any(item => item is
            {
                Scenario: "TA-B2-fresh-request",
                Kind: "request",
                NodeRid: "actor-a",
                PacketName: nameof(ActorAsk)
            } && item.ActorId == actorId && item.Generation == freshRef.Generation),
            "TA-B2 fresh request generation/owner evidence missing.");
    },
    ["TA-B3"] = async () =>
    {
        const string actorId = "ta-b3";
        await PostAsync($"/actors/{actorId}/ensure");
        await CaptureAsync(actorId);
        await callerHttp.Post("/route/disconnect").Body(new { }).SubmitAsync<object>();
        await Task.Delay(250);
        await AssertCachedFailureAsync(
            "TA-B3-disconnected-request",
            actorId,
            "RouteNotConnected");
        var disconnectedEvidence = await GetAllActorEvidenceAsync();
        Require(
            disconnectedEvidence.All(item => item.Scenario != "TA-B3-disconnected-request"),
            "TA-B3 disconnected request unexpectedly reached an actor handler.");
        await callerHttp.Post("/route/reconnect").Body(new { }).SubmitAsync<object>();
        await AssertCallWithRetryAsync(
            "TA-B3-recovered-request",
            actorId,
            "b3-recovered",
            "reply:b3-recovered");
        var recoveredEvidence = await GetAllActorEvidenceAsync();
        Require(
            recoveredEvidence.Any(item => item is
            {
                Scenario: "TA-B3-recovered-request",
                Kind: "request",
                NodeRid: "actor-a",
                PacketName: nameof(ActorAsk)
            } && item.ActorId == actorId),
            "TA-B3 recovered request actor-owner evidence missing.");
    }
};

foreach (var name in SelectedScenarioNames(options.Scenario, scenarios.Keys))
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");

    await scenario();
}

var actorAEvidence = (await actorHttp.Get("/evidence").SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
                     ?? throw new InvalidOperationException("Actor-a evidence endpoint returned null.");
var actorBEvidence = (await actorBHttp.Get("/evidence").SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
                     ?? throw new InvalidOperationException("Actor-b evidence endpoint returned null.");
var evidence = actorAEvidence.Concat(actorBEvidence).ToArray();
RequireEvidenceIfSelected("TA-A1", "TA-A1-send", "send", "TA-A1 send evidence missing.");
RequireEvidenceIfSelected("TA-A1", "TA-A1-request", "request", "TA-A1 request evidence missing.");
if (ScenarioSelected(options.Scenario, "TA-A1", scenarios.Keys))
{
    Require(
        evidence.Any(item => item is { Scenario: "TA-A1", Kind: "bound-push", Value: "BeforeNotify" }),
        "TA-A1 BeforeNotify bound push evidence missing.");
    Require(
        evidence.Any(item => item is { Scenario: "TA-A1", Kind: "bound-push", Value: "AfterNotify" }),
        "TA-A1 AfterNotify bound push evidence missing.");
}
RequireEvidenceIfSelected("TA-A2", "TA-A2-unbound-send", "send", "TA-A2 send evidence missing.");
RequireEvidenceIfSelected("TA-A3", "TA-A3-before-bind-send", "send", "TA-A3 pre-bind send evidence missing.");
RequireEvidenceIfSelected("TA-A3", "TA-A3-before-bind-request", "request", "TA-A3 pre-bind request evidence missing.");
RequireEvidenceIfSelected("TA-A3", "TA-A3-after-bind-send", "send", "TA-A3 post-bind send evidence missing.");
RequireEvidenceIfSelected("TA-A3", "TA-A3-after-bind-request", "request", "TA-A3 post-bind request evidence missing.");
RequireEvidenceIfSelected("TA-A3", "TA-A3", "bound-push", "TA-A3 bound push evidence missing.");
RequireEvidenceIfSelected("TA-A4", "TA-A4-disconnected-send", "send", "TA-A4 send evidence missing.");
RequireEvidenceIfSelected("TA-A4", "TA-A4-disconnected-request", "request", "TA-A4 request evidence missing.");
RequireEvidenceIfSelected("TA-A4", "TA-A4", "destroy", "TA-A4 destroy evidence missing.");

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

async Task<ActorRefSnapshot> CaptureAsync(string actorId)
{
    return (await callerHttp.Post($"/refs/{actorId}/capture")
        .Body(new { })
        .SubmitAsync<ActorRefSnapshot>()).Body;
}

async Task<ActorEvidence[]> GetAllActorEvidenceAsync()
{
    var actorA = (await actorHttp.Get("/evidence").SubmitAsync<ActorEvidence[]>()).Body;
    var actorB = (await actorBHttp.Get("/evidence").SubmitAsync<ActorEvidence[]>()).Body;
    return actorA.Concat(actorB).ToArray();
}

async Task AssertRouteAbsentAsync(string actorId)
{
    var status = (await callerHttp.Get($"/directory/{actorId}")
        .SubmitAsync<ActorRouteStatus>()).Body;
    Require(!status.Exists, $"Actor route '{actorId}' was created unexpectedly.");
}

async Task AssertCachedFailureAsync(string scenario, string actorId, string expectedKind)
{
    var response = await PostJsonAsync<ActorCallResponse>(
        "/cached/request",
        new ActorCallRequest(scenario, actorId, "failure"));
    Require(
        response.ErrorKind == expectedKind,
        $"{scenario} expected '{expectedKind}', got '{response.ErrorKind}'.");
}

async Task AssertCachedFailureWithRetryAsync(string scenario, string actorId, string expectedKind)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    ActorCallResponse? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        last = await PostJsonAsync<ActorCallResponse>(
            "/cached/request",
            new ActorCallRequest(scenario, actorId, "failure"));
        if (last.ErrorKind == expectedKind) return;
        await Task.Delay(100);
    }
    throw new InvalidOperationException(
        $"{scenario} expected '{expectedKind}', got '{last?.ErrorKind}'.");
}

async Task<IZlinkStreamConnector> ConnectAsync(string endpoint)
{
    var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
    {
        Endpoint = new Uri(endpoint),
        ConnectTimeout = TimeSpan.FromSeconds(5),
        RequestTimeout = TimeSpan.FromSeconds(10),
        Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
        DispatchMode = ZlinkStreamDispatchMode.Immediate,
        MaxReceivedMessages = 128
    });
    try
    {
        await connector.Connect.Async();
        return connector;
    }
    catch
    {
        await connector.DisposeAsync();
        throw;
    }
}

async Task<IZlinkStreamConnector> ConnectAndBindAsync(string endpoint, string actorId)
{
    var connector = await ConnectAsync(endpoint);
    try
    {
        var reply = await connector.Request(new BindActorRequest(actorId))
            .PacketName("BindActorRequest")
            .Async<BindActorReply>();
        Require(reply.ActorId == actorId, $"Actor bind reply mismatch for '{actorId}'.");
        var probe = await connector.Request(new ActorAsk("bind-probe", actorId, "bound"))
            .PacketName("ActorAsk")
            .Async<ActorReply>();
        Require(probe.ActorId == actorId, $"Actor bind probe mismatch for '{actorId}'.");
        return connector;
    }
    catch
    {
        await connector.DisposeAsync();
        throw;
    }
}

async Task AssertBoundPushAsync(
    IZlinkStreamConnector bound,
    IZlinkStreamConnector? unbound,
    string scenario,
    string actorId,
    string value)
{
    var received = bound.WaitFor<BoundPushNotify>().Async().AsTask();
    var unexpected = unbound?.WaitFor<BoundPushNotify>()
        .Timeout(TimeSpan.FromMilliseconds(300))
        .Async()
        .AsTask();
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    BoundPushReply? reply = null;
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            reply = (await actorHttp.Post($"/actors/{actorId}/push")
                .Body(new BoundPushRequest(scenario, actorId, value))
                .SubmitAsync<BoundPushReply>()).Body;
            break;
        }
        catch (Exception error)
        {
            last = error;
            await Task.Delay(100);
        }
    }
    if (reply is null)
        throw new InvalidOperationException($"{scenario} bound session did not become ready.", last);
    var notify = await received;
    Require(reply.Submitted, $"{scenario} bound push was not submitted.");
    Require(
        notify.Payload == new BoundPushNotify(scenario, actorId, value),
        $"{scenario} bound push payload mismatch.");
    if (unexpected is not null)
    {
        var timedOut = false;
        try
        {
            await unexpected;
        }
        catch (TimeoutException)
        {
            timedOut = true;
        }
        Require(timedOut, $"{scenario} unbound session received a bound push.");
    }
}

async Task AssertBoundPushFailureAsync(
    ZLinkHttpClient actor,
    string scenario,
    string actorId,
    string value)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    BoundPushReply? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        last = (await actor.Post($"/actors/{actorId}/push")
            .Body(new BoundPushRequest(scenario, actorId, value))
            .SubmitAsync<BoundPushReply>()).Body;
        if (!last.Submitted && last.ErrorKind == "ActorSessionNotBound") return;
        await Task.Delay(100);
    }
    throw new InvalidOperationException(
        $"{scenario} expected ActorSessionNotBound after disconnect, got '{last?.ErrorKind}'.");
}

async Task WaitForSessionEvidenceAsync(
    ZLinkHttpClient session,
    string marker,
    string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    while (DateTimeOffset.UtcNow < deadline)
    {
        var entries = (await session.Get("/evidence").SubmitAsync<string[]>()).Body;
        if (entries.Any(entry => entry.Contains(marker, StringComparison.Ordinal))) return;
        await Task.Delay(100);
    }
    throw new InvalidOperationException(failureMessage);
}

async Task AssertNoActorEvidenceAsync(string actorId)
{
    await Task.Delay(300);
    var entries = (await actorHttp.Get("/evidence").SubmitAsync<ActorEvidence[]>()).Body;
    Require(
        entries.All(item => item.ActorId != actorId),
        $"Missing actor '{actorId}' unexpectedly produced handler or lifecycle evidence.");
}

async Task AssertCallWithRetryAsync(
    string scenario,
    string actorId,
    string value,
    string expected)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    ActorCallResponse? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        last = await PostJsonAsync<ActorCallResponse>(
            "/request",
            new ActorCallRequest(scenario, actorId, value));
        if (last.ErrorKind is null && last.Result == expected)
            return;
        await Task.Delay(100);
    }

    throw new InvalidOperationException(
        $"{scenario} did not recover; result='{last?.Result}', error='{last?.ErrorKind}'.");
}

async Task PostAsync(string path)
{
    await actorHttp.Post(path).Body(new { }).SubmitAsync<object>();
}

async Task PostWithRetryAsync(string path)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            await PostAsync(path);
            return;
        }
        catch (Exception error)
        {
            last = error;
            await Task.Delay(100);
        }
    }

    throw new InvalidOperationException($"Endpoint '{path}' did not become ready.", last);
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

internal sealed record ClientOptions(
    string ActorUrl,
    string ActorBUrl,
    string CallerUrl,
    string SessionAStreamEndpoint,
    string SessionBStreamEndpoint,
    string SessionAUrl,
    string SessionBUrl,
    string Scenario)
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
            values["actor-b-url"],
            values["caller-url"],
            values["session-a-stream-endpoint"],
            values["session-b-stream-endpoint"],
            values["session-a-url"],
            values["session-b-url"],
            values.TryGetValue("scenario", out var scenario) ? scenario : "all");
    }
}
