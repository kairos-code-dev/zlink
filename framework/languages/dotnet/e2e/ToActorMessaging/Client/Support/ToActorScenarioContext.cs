using Systems.Zlink.Stream.Connector.Contracts;
using ToActorMessaging.Shared;
using Zlink.HttpClient;
using Zlink.Framework.Contracts.Actors;

namespace ToActorMessaging.Client.Support;

internal sealed class ToActorScenarioContext : IDisposable
{
    private readonly ZLinkHttpClient _actorHttp;
    private readonly ZLinkHttpClient _actorBHttp;
    private readonly ZLinkHttpClient _callerHttp;
    private readonly ZLinkHttpClient _sessionAHttp;
    private readonly ZLinkHttpClient _sessionBHttp;

    public ToActorScenarioContext(ClientOptions options)
    {
        Options = options;
        _actorHttp = CreateClient(options.ActorUrl);
        _actorBHttp = CreateClient(options.ActorBUrl);
        _callerHttp = CreateClient(options.CallerUrl);
        _sessionAHttp = CreateClient(options.SessionAUrl);
        _sessionBHttp = CreateClient(options.SessionBUrl);
    }

    public ClientOptions Options { get; }

    public void Dispose()
    {
        _actorHttp.Dispose();
        _actorBHttp.Dispose();
        _callerHttp.Dispose();
        _sessionAHttp.Dispose();
        _sessionBHttp.Dispose();
    }

    public Task EnsureActorAAsync(string actorId) => PostActorAAsync($"/actors/{actorId}/ensure");

    public async Task EnsureActorBAsync(string actorId)
    {
        await _actorBHttp.Post($"/actors/{actorId}/ensure").Body(new { }).SubmitAsync<object>();
    }

    public async Task DestroyActorAAsync(string actorId, string scenario)
    {
        await _actorHttp.Post($"/actors/{actorId}/destroy?scenario={scenario}")
            .Body(new { })
            .SubmitAsync<DestroyActorReply>();
    }

    public async Task DisconnectCallerAsync()
    {
        await _callerHttp.Post("/route/disconnect").Body(new { }).SubmitAsync<object>();
    }

    public async Task ReconnectCallerAsync()
    {
        await _callerHttp.Post("/route/reconnect").Body(new { }).SubmitAsync<object>();
    }

    public async Task AssertCallAsync(string scenario, string actorId, string value, string expected, bool send)
    {
        var endpoint = send ? "send" : "request";
        var response = await PostJsonAsync<ActorCallResponse>(
            $"/{endpoint}", new ActorCallRequest(scenario, actorId, value));
        Require(response.Result == expected, $"{scenario} expected '{expected}', got '{response.Result}'.");
        Require(response.ErrorKind is null, $"{scenario} unexpected error '{response.ErrorKind}'.");
    }

    public async Task AssertFailureAsync(string scenario, string actorId, string expectedKind, bool send)
    {
        var endpoint = send ? "send" : "request";
        var response = await PostJsonAsync<ActorCallResponse>(
            $"/{endpoint}", new ActorCallRequest(scenario, actorId, "missing"));
        Require(response.ErrorKind == expectedKind,
            $"{scenario} expected '{expectedKind}', got '{response.ErrorKind}'.");
    }

    public async Task<ActorRefSnapshot> CaptureAsync(string actorId)
    {
        return (await _callerHttp.Post($"/refs/{actorId}/capture")
            .Body(new { })
            .SubmitAsync<ActorRefSnapshot>()).Body;
    }

    public async Task<ActorEvidence[]> GetAllActorEvidenceAsync()
    {
        var actorA = (await _actorHttp.Get("/evidence").SubmitAsync<ActorEvidence[]>()).Body;
        var actorB = (await _actorBHttp.Get("/evidence").SubmitAsync<ActorEvidence[]>()).Body;
        return actorA.Concat(actorB).ToArray();
    }

    public async Task AssertRouteAbsentAsync(string actorId)
    {
        var status = (await _callerHttp.Get($"/directory/{actorId}")
            .SubmitAsync<ActorRouteStatus>()).Body;
        Require(!status.Exists, $"Actor route '{actorId}' was created unexpectedly.");
    }

    public async Task AssertCachedFailureAsync(string scenario, string actorId, string expectedKind)
    {
        var response = await PostJsonAsync<ActorCallResponse>(
            "/cached/request", new ActorCallRequest(scenario, actorId, "failure"));
        Require(response.ErrorKind == expectedKind,
            $"{scenario} expected '{expectedKind}', got '{response.ErrorKind}'.");
    }

    public async Task AssertCachedFailureWithRetryAsync(string scenario, string actorId, string expectedKind)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        ActorCallResponse? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            last = await PostJsonAsync<ActorCallResponse>(
                "/cached/request", new ActorCallRequest(scenario, actorId, "failure"));
            if (last.ErrorKind == expectedKind) return;
            await Task.Delay(100);
        }

        throw new InvalidOperationException($"{scenario} expected '{expectedKind}', got '{last?.ErrorKind}'.");
    }

    public async Task<IZlinkStreamConnector> ConnectAsync(string endpoint)
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

    public async Task<IZlinkStreamConnector> ConnectAndBindAsync(string endpoint, string actorId)
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

    public async Task AssertBoundPushAsync(
        IZlinkStreamConnector bound,
        IZlinkStreamConnector? unbound,
        string scenario,
        string actorId,
        string value)
    {
        var received = bound.WaitFor<BoundPushNotify>().Async().AsTask();
        var unexpected = unbound?.WaitFor<BoundPushNotify>()
            .Timeout(TimeSpan.FromMilliseconds(300)).Async().AsTask();
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        BoundPushReply? reply = null;
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                reply = (await _actorHttp.Post($"/actors/{actorId}/push")
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

        if (reply is null) throw new InvalidOperationException($"{scenario} bound session did not become ready.", last);
        var notify = await received;
        Require(reply.Submitted, $"{scenario} bound push was not submitted.");
        Require(notify.Payload == new BoundPushNotify(scenario, actorId, value),
            $"{scenario} bound push payload mismatch.");
        if (unexpected is null) return;

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

    public async Task AssertBoundPushFailureAsync(
        bool actorB,
        string scenario,
        string actorId,
        string value)
    {
        var actor = actorB ? _actorBHttp : _actorHttp;
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

    public async Task WaitForSessionAEvidenceAsync(string marker, string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var entries = (await _sessionAHttp.Get("/evidence").SubmitAsync<string[]>()).Body;
            if (entries.Any(entry => entry.Contains(marker, StringComparison.Ordinal))) return;
            await Task.Delay(100);
        }
        throw new InvalidOperationException(failureMessage);
    }

    public async Task AssertNoActorEvidenceAsync(string actorId)
    {
        await Task.Delay(300);
        var entries = (await _actorHttp.Get("/evidence").SubmitAsync<ActorEvidence[]>()).Body;
        Require(entries.All(item => item.ActorId != actorId),
            $"Missing actor '{actorId}' unexpectedly produced handler or lifecycle evidence.");
    }

    public async Task AssertCallWithRetryAsync(string scenario, string actorId, string value, string expected)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        ActorCallResponse? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            last = await PostJsonAsync<ActorCallResponse>(
                "/request", new ActorCallRequest(scenario, actorId, value));
            if (last.ErrorKind is null && last.Result == expected) return;
            await Task.Delay(100);
        }
        throw new InvalidOperationException(
            $"{scenario} did not recover; result='{last?.Result}', error='{last?.ErrorKind}'.");
    }

    public async Task PostActorAWithRetryAsync(string path)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                await PostActorAAsync(path);
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

    public static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private async Task PostActorAAsync(string path)
    {
        await _actorHttp.Post(path).Body(new { }).SubmitAsync<object>();
    }

    private async Task<T> PostJsonAsync<T>(string path, object body)
    {
        return (await _callerHttp.Post(path).Body(body).SubmitAsync<T>()).Body
               ?? throw new InvalidOperationException($"Endpoint '{path}' returned null.");
    }

    private static ZLinkHttpClient CreateClient(string url) =>
        ZLinkHttpClient.Create(url).Timeout(TimeSpan.FromSeconds(30)).Build();
}
