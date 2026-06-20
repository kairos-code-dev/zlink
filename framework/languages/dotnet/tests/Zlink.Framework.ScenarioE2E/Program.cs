using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.HttpClient;

if (args.Length == 0)
{
    throw new InvalidOperationException("scenario E2E role is required.");
}

var role = args[0];
var options = ScenarioOptions.Parse(args.Skip(1).ToArray());

switch (role)
{
    case "server":
        await RunServerAsync(options);
        break;
    case "client":
        await RunClientAsync(options);
        break;
    default:
        throw new InvalidOperationException($"Unknown scenario E2E role '{role}'.");
}

static async Task RunServerAsync(ScenarioOptions options)
{
    Directory.CreateDirectory(options.WorkDir);
    var evidence = new ScenarioEvidence(options.ServerName);

    var builder = WebApplication.CreateBuilder();
    builder.Logging.ClearProviders();
    builder.WebHost.UseUrls(options.HttpEndpoint);
    builder.Services.AddSingleton(evidence);
    builder.Services.AddZLinkFramework(framework =>
    {
        if (options.Mode == "route-peer")
        {
            var routed = framework.AddRouteMeshChannel(options.Channel);
            routed.EnableServer(options.ZLinkEndpoint);
            routed.ConfigureRouting().RoutingId = RoutingId.From(options.ServerName);
            foreach (var endpoint in options.RoutePeerEndpoints)
            {
                routed.EnableClient(endpoint);
            }

            routed.AddRequestHandler<ScenarioRoutePingHandler, ScenarioRoutePingRequest, ScenarioRoutePingReply>(
                "ScenarioRoutePing");
            return;
        }

        var channel = framework.AddClientServerChannel(options.Channel);
        channel.EnableServer(options.ZLinkEndpoint);
        channel.AddRequestHandler<ScenarioProfileHandler, ScenarioProfileRequest, ScenarioProfileReply>(
            "ScenarioProfileRequest");
        channel.AddSendHandler<ScenarioProfileCommandHandler, ScenarioProfileCommand>(
            "ScenarioProfileCommand");
    });

    var app = builder.Build();
    app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
    app.MapGet("/evidence", (ScenarioEvidence serverEvidence) =>
        Results.Json(new ScenarioEvidenceSnapshot(
            serverEvidence.ServerId,
            serverEvidence.RequestIds.ToArray(),
            serverEvidence.CompletedRequestIds.ToArray(),
            serverEvidence.CommandIds.ToArray(),
            serverEvidence.RouteRequestIds.ToArray(),
            serverEvidence.RouteSourceRids.ToArray())));

    await File.WriteAllTextAsync(options.ReadyFile, "ready");
    await app.RunAsync();
}

static async Task RunClientAsync(ScenarioOptions options)
{
    Directory.CreateDirectory(options.WorkDir);
    var scenario = await ReadScenarioAsync(options.ScenarioFile);

    ensure(scenario.Id is "CH-001" or "CH-002" or "CH-004" or "CH-006" or "CH-007", "scenario id must be supported");
    ensure(scenario.Roles.Server.Any(server => server.Channel == options.Channel),
        "scenario server channel must match the configured channel");
    ensure(scenario.Roles.Client.Length > 0, "scenario must declare at least one client role");
    ensure(Path.GetFullPath(Path.Combine(options.WorkDir, scenario.Artifacts.Logs)) == Path.GetFullPath(options.LogDir),
        "scenario logs artifact must match configured log directory");
    ensure(Path.GetFullPath(Path.Combine(options.WorkDir, scenario.Artifacts.Report)) == Path.GetFullPath(options.ReportFile),
        "scenario report artifact must match configured report file");
    ensure(scenario.Artifacts.Evidence is "server:/evidence" or "servers:/evidence",
        "scenario evidence artifact must use supported evidence endpoint");

    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();
    builder.Services.AddZLinkFramework(framework =>
    {
        if (scenario.Id == "CH-004")
        {
            var routed = framework.AddRouteMeshChannel(options.Channel);
            routed.EnableServer(options.ZLinkEndpoint);
            routed.ConfigureRouting().RoutingId = RoutingId.From(
                scenario.Roles.Client[0].RoutingId ?? scenario.Roles.Client[0].Name);
            foreach (var endpoint in options.RoutePeerEndpoints)
            {
                routed.EnableClient(endpoint);
            }

            return;
        }

        var channel = framework.AddClientServerChannel(options.Channel);
        foreach (var endpoint in options.ZLinkEndpoints)
        {
            channel.EnableClient(endpoint);
        }
    });

    using var host = builder.Build();
    await host.StartAsync();

    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    using var http = ZLinkHttpClient.Create(options.HttpEndpoints[0])
        .Json()
        .Timeout(TimeSpan.FromSeconds(3))
        .Build();
    var httpClients = options.HttpEndpoints
        .Select(endpoint => ZLinkHttpClient.Create(endpoint).Json().Timeout(TimeSpan.FromSeconds(3)).Build())
        .ToArray();
    var repliesByServer = new Dictionary<string, int>(StringComparer.Ordinal);

    try
    {
        foreach (var step in scenario.Steps)
        {
            if (step.WaitReady is { } waitReady)
            {
                var targets = waitReady.Targets ?? new[] { waitReady.Target ?? "" };
                var selectedHttpClients = targets.Length == 1 ? new[] { http } : httpClients;
                ensure(selectedHttpClients.Length == targets.Length, "readiness endpoint count must match server targets");
                var serverReady = false;
                var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
                while (DateTimeOffset.UtcNow < deadline && !serverReady)
                {
                    serverReady = true;
                    foreach (var endpoint in selectedHttpClients)
                    {
                        try
                        {
                            var response = await endpoint.Get("/health").SubmitRawAsync();
                            serverReady = serverReady && response.Status == (int)HttpStatusCode.OK;
                        }
                        catch
                        {
                            serverReady = false;
                        }
                    }

                    if (!serverReady)
                    {
                        await Task.Delay(100);
                    }
                }

                ensure(targets.All(target => target.StartsWith("server:", StringComparison.Ordinal)),
                    "readiness targets must be server roles");
                ensure(serverReady, "server readiness must be reached before client request");
                continue;
            }

            if (step.ClientRequest is { } request)
            {
                ensure(request.Client == scenario.Roles.Client[0].Name, "request client must match scenario role");
                ensure(request.Channel == options.Channel, "request channel must match configured channel");
                ensure(request.PacketName == "ScenarioProfileRequest", "request packet name must match handler");

                var reply = await client
                    .RequestToChannel(request.Channel, new ScenarioProfileRequest(request.RequestId, request.Value))
                    .PacketName(request.PacketName)
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ScenarioProfileReply>();

                ensure(reply.RequestId == request.RequestId, "reply request id must match");
                ensure(reply.Value == request.ExpectReply, "reply value must match scenario expectation");
                if (!string.IsNullOrWhiteSpace(scenario.Expect.Reply.RequestId)
                    && scenario.Expect.Reply.RequestId == request.RequestId)
                {
                    ensure(reply.RequestId == scenario.Expect.Reply.RequestId, "reply request id must match expect block");
                    ensure(reply.Value == scenario.Expect.Reply.Value, "reply value must match expect block");
                }
                continue;
            }

            if (step.ClientTimeoutRequest is { } timeoutRequest)
            {
                ensure(timeoutRequest.Client == scenario.Roles.Client[0].Name, "timeout request client must match scenario role");
                ensure(timeoutRequest.Channel == options.Channel, "timeout request channel must match configured channel");
                ensure(timeoutRequest.PacketName == "ScenarioProfileRequest", "timeout request packet name must match handler");
                ensure(timeoutRequest.TimeoutMs > 0, "timeout request must set a positive timeout");
                ensure(timeoutRequest.ExpectError == "timeout", "timeout request expected error must be timeout");
                try
                {
                    _ = await client
                        .RequestToChannel(
                            timeoutRequest.Channel,
                            new ScenarioProfileRequest(timeoutRequest.RequestId, timeoutRequest.Value))
                        .PacketName(timeoutRequest.PacketName)
                        .Timeout(TimeSpan.FromMilliseconds(timeoutRequest.TimeoutMs))
                        .Async<ScenarioProfileReply>();
                    throw new InvalidOperationException("timeout request must fail");
                }
                catch (TimeoutException)
                {
                }

                continue;
            }

            if (step.ConcurrentTimeoutAndRequest is { } concurrent)
            {
                ensure(concurrent.Client == scenario.Roles.Client[0].Name, "concurrent request client must match scenario role");
                ensure(concurrent.Channel == options.Channel, "concurrent request channel must match configured channel");
                ensure(concurrent.PacketName == "ScenarioProfileRequest", "concurrent request packet name must match handler");
                ensure(concurrent.TimeoutMs > 0, "concurrent timeout request must set a positive timeout");
                ensure(concurrent.RequestDelayMs >= 0, "concurrent request delay must be non-negative");
                ensure(concurrent.ExpectError == "timeout", "concurrent timeout expected error must be timeout");

                var timeoutTask = client
                    .RequestToChannel(
                        concurrent.Channel,
                        new ScenarioProfileRequest(concurrent.TimeoutRequestId, concurrent.TimeoutValue))
                    .PacketName(concurrent.PacketName)
                    .Timeout(TimeSpan.FromMilliseconds(concurrent.TimeoutMs))
                    .Async<ScenarioProfileReply>()
                    .AsTask();

                if (concurrent.RequestDelayMs > 0)
                {
                    await Task.Delay(concurrent.RequestDelayMs);
                }

                var reply = await client
                    .RequestToChannel(
                        concurrent.Channel,
                        new ScenarioProfileRequest(concurrent.RequestId, concurrent.Value))
                    .PacketName(concurrent.PacketName)
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ScenarioProfileReply>();
                ensure(reply.RequestId == concurrent.RequestId, "concurrent normal reply request id must match");
                ensure(reply.Value == concurrent.ExpectReply, "concurrent normal reply value must match");

                try
                {
                    _ = await timeoutTask;
                    throw new InvalidOperationException("concurrent timeout request must fail");
                }
                catch (TimeoutException)
                {
                }

                continue;
            }

            if (step.WaitFor is { } waitFor)
            {
                ensure(waitFor.Milliseconds > 0, "waitFor must be positive");
                await Task.Delay(waitFor.Milliseconds);
                continue;
            }

            if (step.RoundRobinRequest is { } roundRobin)
            {
                ensure(roundRobin.Client == scenario.Roles.Client[0].Name, "request client must match scenario role");
                ensure(roundRobin.Channel == options.Channel, "request channel must match configured channel");
                ensure(roundRobin.PacketName == "ScenarioProfileRequest", "request packet name must match handler");
                ensure(roundRobin.RequestCount == 90, "CH-002 request count must be 90");
                ensure(roundRobin.WarmupCount == 3, "CH-002 warm-up count must be 3");

                for (var index = 0; index < roundRobin.WarmupCount; index++)
                {
                    var requestId = $"{roundRobin.RequestIdPrefix}-warmup-{index}";
                    var value = $"{roundRobin.ValuePrefix}-warmup-{index}";
                    var reply = await client
                        .RequestToChannel(roundRobin.Channel, new ScenarioProfileRequest(requestId, value))
                        .PacketName(roundRobin.PacketName)
                        .Timeout(TimeSpan.FromSeconds(3))
                        .Async<ScenarioProfileReply>();
                    ensure(reply.RequestId == requestId, "warm-up reply request id must match");
                }

                for (var index = 0; index < roundRobin.RequestCount; index++)
                {
                    var requestId = $"{roundRobin.RequestIdPrefix}-{index}";
                    var value = $"{roundRobin.ValuePrefix}-{index}";
                    var reply = await client
                        .RequestToChannel(roundRobin.Channel, new ScenarioProfileRequest(requestId, value))
                        .PacketName(roundRobin.PacketName)
                        .Timeout(TimeSpan.FromSeconds(3))
                        .Async<ScenarioProfileReply>();
                    ensure(reply.RequestId == requestId, "round-robin reply request id must match");
                    ensure(reply.Value == $"profile:{value}", "round-robin reply value must match");
                    ensure(!string.IsNullOrWhiteSpace(reply.ServerId), "round-robin reply must include server id");
                    repliesByServer[reply.ServerId] = repliesByServer.GetValueOrDefault(reply.ServerId) + 1;
                }

                continue;
            }

            if (step.RouteTargetedRequest is { } routeRequest)
            {
                var routeClient = host.Services.GetRequiredService<IZLinkRouteClient>();
                ensure(routeRequest.Client == scenario.Roles.Client[0].Name, "route request client must match scenario role");
                ensure(routeRequest.Channel == options.Channel, "route request channel must match configured channel");
                ensure(routeRequest.PacketName == "ScenarioRoutePing", "route request packet name must match handler");

                var reply = await routeClient
                    .Request(
                        routeRequest.Channel,
                        RoutingId.From(routeRequest.TargetRid),
                        new ScenarioRoutePingRequest(routeRequest.RequestId, routeRequest.Value))
                    .PacketName(routeRequest.PacketName)
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ScenarioRoutePingReply>();

                ensure(reply.RequestId == routeRequest.RequestId, "route reply request id must match");
                ensure(reply.Value == routeRequest.ExpectReply, "route reply value must match");
                ensure(reply.TargetRid == routeRequest.TargetRid, "route reply target routing id must match");
                ensure(reply.SourceRid == routeRequest.SourceRid, "route reply source routing id must match");

                try
                {
                    _ = await routeClient
                        .Request(
                            routeRequest.Channel,
                            RoutingId.From(routeRequest.MissingRid),
                            new ScenarioRoutePingRequest($"{routeRequest.RequestId}-missing", routeRequest.Value))
                        .PacketName(routeRequest.PacketName)
                        .Timeout(TimeSpan.FromMilliseconds(routeRequest.MissingTimeoutMs))
                        .Async<ScenarioRoutePingReply>();
                    throw new InvalidOperationException("missing route request must fail with public error");
                }
                catch (Exception ex) when (
                    ex is TimeoutException
                    || ex.Message.Contains("timeout", StringComparison.OrdinalIgnoreCase)
                    || ex.Message.Contains("not connected", StringComparison.OrdinalIgnoreCase)
                    || ex.Message.Contains("not ready", StringComparison.OrdinalIgnoreCase)
                    || ex.Message.Contains("host unreachable", StringComparison.OrdinalIgnoreCase)
                    || ex.Message.Contains("RouteNotConnected", StringComparison.OrdinalIgnoreCase))
                {
                }

                var httpByServer = scenario.Roles.Server
                    .Select((server, index) => (server.Name, Client: httpClients[index]))
                    .ToDictionary(pair => pair.Name, pair => pair.Client, StringComparer.Ordinal);
                ensure(httpByServer.TryGetValue(routeRequest.TargetRid, out var targetHttp),
                    "target route peer evidence endpoint must exist");
                ensure(httpByServer.TryGetValue(routeRequest.NonTargetRid, out var otherHttp),
                    "non-target route peer evidence endpoint must exist");

                var targetMatched = false;
                var targetEvidence = new ScenarioEvidenceSnapshot("", [], [], [], [], []);
                var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
                while (DateTimeOffset.UtcNow < deadline && !targetMatched)
                {
                    targetEvidence = (await targetHttp.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
                    targetMatched = targetEvidence.RouteRequestIds.Contains(routeRequest.RequestId)
                        && targetEvidence.RouteSourceRids.Contains(routeRequest.SourceRid);
                    if (!targetMatched)
                    {
                        await Task.Delay(100);
                    }
                }

                ensure(targetMatched, "target route peer evidence must include request and source routing id");
                ensure((scenario.Expect.ServerEvidence.RouteRequestIds ?? []).All(targetEvidence.RouteRequestIds.Contains),
                    "target route peer evidence must include expected route request ids");
                ensure((scenario.Expect.ServerEvidence.RouteSourceRids ?? []).All(targetEvidence.RouteSourceRids.Contains),
                    "target route peer evidence must include expected source routing ids");
                var nonTargetDeadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(1);
                while (DateTimeOffset.UtcNow < nonTargetDeadline)
                {
                    var otherEvidence = (await otherHttp.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
                    ensure(!otherEvidence.RouteRequestIds.Contains(routeRequest.RequestId),
                        "non-target route peer evidence must not include targeted request");
                    ensure(!(scenario.Expect.ServerEvidence.RouteRequestIds ?? []).Any(otherEvidence.RouteRequestIds.Contains),
                        "non-target route peer evidence must not include expected route request ids");
                    await Task.Delay(100);
                }

                continue;
            }

            if (step.AssertEvidence is { } evidenceStep)
            {
                ensure(evidenceStep.Target == "server:api-a", "evidence target must be server:api-a");
                var evidence = await WaitForEvidenceAsync(http, evidenceStep);
                if (!string.IsNullOrWhiteSpace(evidenceStep.RequestId))
                {
                    ensure(evidence.RequestIds.Contains(evidenceStep.RequestId),
                        "server evidence must include request id from step");
                }

                if (!string.IsNullOrWhiteSpace(evidenceStep.CommandId))
                {
                    ensure(evidence.CommandIds.Contains(evidenceStep.CommandId),
                        "server evidence must include command id from step");
                }

                ensure((scenario.Expect.ServerEvidence.RequestIds ?? []).All(evidence.RequestIds.Contains),
                    "server evidence must include all expected request ids");
                ensure((scenario.Expect.ServerEvidence.CompletedRequestIds ?? []).All(evidence.CompletedRequestIds.Contains),
                    "server evidence must include all expected completed request ids");
                ensure((scenario.Expect.ServerEvidence.CommandIds ?? []).All(evidence.CommandIds.Contains),
                    "server evidence must include all expected command ids");
                continue;
            }

            if (step.ClientSend is { } send)
            {
                ensure(send.Client == scenario.Roles.Client[0].Name, "send client must match scenario role");
                ensure(send.Channel == options.Channel, "send channel must match configured channel");
                ensure(send.PacketName == "ScenarioProfileCommand", "send packet name must match handler");
                await client
                    .SendToChannel(send.Channel, new ScenarioProfileCommand(send.CommandId, send.Value))
                    .PacketName(send.PacketName)
                    .Async();
                continue;
            }

            if (step.AssertDistribution is { } distribution)
            {
                foreach (var expected in distribution.ExpectedCounts)
                {
                    ensure(repliesByServer.GetValueOrDefault(expected.Key) == expected.Value,
                        $"round-robin reply count for {expected.Key} must be {expected.Value}");
                }

                foreach (var endpoint in httpClients)
                {
                    var evidence = await endpoint.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>();
                    ensure(distribution.ExpectedCounts.TryGetValue(evidence.Body.ServerId, out var expectedCount),
                        "server evidence must identify a known server");
                    var verifiedRequests = evidence.Body.RequestIds
                        .Where(requestId =>
                            requestId.StartsWith($"{scenario.Expect.Reply.RequestIdPrefix}-", StringComparison.Ordinal)
                            && !requestId.Contains("-warmup-", StringComparison.Ordinal))
                        .ToArray();
                    ensure(verifiedRequests.Length == expectedCount,
                        $"server evidence count for {evidence.Body.ServerId} must be {expectedCount}");
                }

                continue;
            }

            throw new InvalidOperationException("scenario step must contain a known action.");
        }
    }
    finally
    {
        foreach (var endpoint in httpClients)
        {
            endpoint.Dispose();
        }
    }

    var report = new ScenarioReport(
        scenario.Id,
        "dotnet",
        RuntimeInformation.FrameworkDescription,
        "tcp",
        "json",
        "passed",
        options.ScenarioFile,
        scenario.Roles.Server.Length + scenario.Roles.Client.Length,
        1,
        0,
        0,
        options.LogDir,
        string.Join(',', options.HttpEndpoints.Select(endpoint => $"{endpoint}/evidence")),
        options.ExecutedScriptPath,
        options.ProcessIds(scenario.Roles.Client),
        null,
        DateTimeOffset.UtcNow);
    await File.WriteAllTextAsync(options.ReportFile, JsonSerializer.Serialize(report, ScenarioJson.Options));
    await host.StopAsync();

    Console.WriteLine($"scenario={scenario.Id} language=dotnet result=passed evidence={options.ReportFile}");

    static void ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"ensure failed: {message}");
        }
    }
}

static async Task<ScenarioEvidenceSnapshot> WaitForEvidenceAsync(
    ZLinkHttpClient http,
    ScenarioAssertEvidenceStep evidenceStep)
{
    var last = new ScenarioEvidenceSnapshot("", [], [], [], [], []);
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    while (DateTimeOffset.UtcNow < deadline)
    {
        last = (await http.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
        var requestMatched = string.IsNullOrWhiteSpace(evidenceStep.RequestId)
            || last.RequestIds.Contains(evidenceStep.RequestId);
        var commandMatched = string.IsNullOrWhiteSpace(evidenceStep.CommandId)
            || last.CommandIds.Contains(evidenceStep.CommandId);
        if (requestMatched && commandMatched)
        {
            return last;
        }

        await Task.Delay(100);
    }

    return last;
}

static async Task<ScenarioDocument> ReadScenarioAsync(string scenarioFile)
{
    await using var stream = File.OpenRead(scenarioFile);
    return await JsonSerializer.DeserializeAsync<ScenarioDocument>(stream, ScenarioJson.Options)
           ?? throw new InvalidOperationException("scenario file is empty.");
}

internal sealed record ScenarioProfileRequest(string RequestId, string Value);

internal sealed record ScenarioProfileReply(string RequestId, string Value, string ServerId);

internal sealed record ScenarioProfileCommand(string CommandId, string Value);

internal sealed record ScenarioRoutePingRequest(string RequestId, string Value);

internal sealed record ScenarioRoutePingReply(string RequestId, string Value, string TargetRid, string SourceRid);

internal sealed class ScenarioProfileHandler(ScenarioEvidence evidence)
    : IZLinkRequestHandler<ScenarioProfileRequest, ScenarioProfileReply>
{
    public ValueTask<ScenarioProfileReply> HandleAsync(
        ScenarioProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.RequestIds.Enqueue(request.RequestId);
        return HandleCoreAsync(request, cancellationToken);
    }

    private async ValueTask<ScenarioProfileReply> HandleCoreAsync(
        ScenarioProfileRequest request,
        CancellationToken cancellationToken)
    {
        if (request.Value == "slow" || request.RequestId.Contains("timeout", StringComparison.Ordinal))
        {
            await Task.Delay(1000, cancellationToken);
        }

        evidence.CompletedRequestIds.Enqueue(request.RequestId);
        return new ScenarioProfileReply(request.RequestId, $"profile:{request.Value}", evidence.ServerId);
    }
}

internal sealed class ScenarioRoutePingHandler(ScenarioEvidence evidence)
    : IZLinkRouteRequestHandler<ScenarioRoutePingRequest, ScenarioRoutePingReply>
{
    public ValueTask<ScenarioRoutePingReply> HandleAsync(
        ScenarioRoutePingRequest request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.RouteRequestIds.Enqueue(request.RequestId);
        evidence.RouteSourceRids.Enqueue(context.SourceNodeRid.ToString());
        return ValueTask.FromResult(new ScenarioRoutePingReply(
            request.RequestId,
            $"route:{request.Value}",
            evidence.ServerId,
            context.SourceNodeRid.ToString()));
    }
}

internal sealed class ScenarioProfileCommandHandler(ScenarioEvidence evidence)
    : IZLinkSendHandler<ScenarioProfileCommand>
{
    public ValueTask HandleAsync(
        ScenarioProfileCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.CommandIds.Enqueue(message.CommandId);
        return ValueTask.CompletedTask;
    }
}

internal sealed class ScenarioEvidence(string serverId)
{
    public string ServerId { get; } = serverId;

    public ConcurrentQueue<string> RequestIds { get; } = new();

    public ConcurrentQueue<string> CompletedRequestIds { get; } = new();

    public ConcurrentQueue<string> CommandIds { get; } = new();

    public ConcurrentQueue<string> RouteRequestIds { get; } = new();

    public ConcurrentQueue<string> RouteSourceRids { get; } = new();
}

internal sealed record ScenarioEvidenceSnapshot(
    string ServerId,
    string[] RequestIds,
    string[] CompletedRequestIds,
    string[] CommandIds,
    string[] RouteRequestIds,
    string[] RouteSourceRids);

internal sealed record ScenarioDocument(
    string Id,
    string Name,
    ScenarioRoles Roles,
    ScenarioStep[] Steps,
    ScenarioExpect Expect,
    ScenarioArtifacts Artifacts);

internal sealed record ScenarioRoles(
    ScenarioRegistryRole Registry,
    ScenarioServerRole[] Server,
    ScenarioClientRole[] Client);

internal sealed record ScenarioRegistryRole(int Count);

internal sealed record ScenarioServerRole(string Name, string Channel);

internal sealed record ScenarioClientRole(string Name, string? RoutingId);

internal sealed record ScenarioStep(
    ScenarioWaitReadyStep? WaitReady,
    ScenarioClientRequestStep? ClientRequest,
    ScenarioClientSendStep? ClientSend,
    ScenarioClientTimeoutRequestStep? ClientTimeoutRequest,
    ScenarioConcurrentTimeoutAndRequestStep? ConcurrentTimeoutAndRequest,
    ScenarioWaitForStep? WaitFor,
    ScenarioAssertEvidenceStep? AssertEvidence,
    ScenarioRoundRobinRequestStep? RoundRobinRequest,
    ScenarioRouteTargetedRequestStep? RouteTargetedRequest,
    ScenarioAssertDistributionStep? AssertDistribution);

internal sealed record ScenarioWaitReadyStep(string? Target, string[]? Targets);

internal sealed record ScenarioClientRequestStep(
    string Client,
    string Channel,
    string PacketName,
    string RequestId,
    string Value,
    string ExpectReply);

internal sealed record ScenarioClientSendStep(
    string Client,
    string Channel,
    string PacketName,
    string CommandId,
    string Value);

internal sealed record ScenarioClientTimeoutRequestStep(
    string Client,
    string Channel,
    string PacketName,
    string RequestId,
    string Value,
    int TimeoutMs,
    string ExpectError);

internal sealed record ScenarioConcurrentTimeoutAndRequestStep(
    string Client,
    string Channel,
    string PacketName,
    string TimeoutRequestId,
    string TimeoutValue,
    int TimeoutMs,
    string RequestId,
    string Value,
    string ExpectReply,
    int RequestDelayMs,
    string ExpectError);

internal sealed record ScenarioWaitForStep(int Milliseconds);

internal sealed record ScenarioAssertEvidenceStep(string Target, string? RequestId, string? CommandId);

internal sealed record ScenarioRoundRobinRequestStep(
    string Client,
    string Channel,
    string PacketName,
    int WarmupCount,
    int RequestCount,
    string RequestIdPrefix,
    string ValuePrefix);

internal sealed record ScenarioRouteTargetedRequestStep(
    string Client,
    string Channel,
    string PacketName,
    string TargetRid,
    string NonTargetRid,
    string SourceRid,
    string MissingRid,
    int MissingTimeoutMs,
    string RequestId,
    string Value,
    string ExpectReply);

internal sealed record ScenarioAssertDistributionStep(Dictionary<string, int> ExpectedCounts);

internal sealed record ScenarioExpect(
    ScenarioReplyExpect Reply,
    ScenarioServerEvidenceExpect ServerEvidence);

internal sealed record ScenarioReplyExpect(string? RequestId, string? Value, string? RequestIdPrefix, string? ValuePrefix);

internal sealed record ScenarioServerEvidenceExpect(
    string[]? RequestIds,
    string[]? CompletedRequestIds,
    string[]? CommandIds,
    string[]? RouteRequestIds,
    string[]? RouteSourceRids,
    Dictionary<string, int> RequestCounts);

internal sealed record ScenarioArtifacts(string Logs, string Report, string Evidence);

internal sealed record ScenarioReport(
    string ScenarioId,
    string Language,
    string RuntimeVersion,
    string TransportBackend,
    string Codec,
    string Result,
    string ScenarioFile,
    int ProcessCount,
    int Passed,
    int Failed,
    int Skipped,
    string LogPath,
    string EvidencePath,
    string ExecutedScriptPath,
    Dictionary<string, long> ProcessIds,
    string? FailureLayer,
    DateTimeOffset CompletedAt);

internal sealed class ScenarioOptions
{
    public string WorkDir { get; init; } = "";

    public string ScenarioFile { get; init; } = "";

    public string ZLinkEndpoint { get; init; } = "";

    public string[] ZLinkEndpoints => ZLinkEndpoint.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    public string HttpEndpoint { get; init; } = "";

    public string[] HttpEndpoints => HttpEndpoint.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    public string Channel { get; init; } = "";

    public string ReadyFile { get; init; } = "";

    public string ReportFile { get; init; } = "";

    public string LogDir { get; init; } = "";

    public string ServerName { get; init; } = "api-a";

    public string ExecutedScriptPath { get; init; } = "";

    public string ServerProcessIds { get; init; } = "";

    public string Mode { get; init; } = "channel";

    public string RoutePeerEndpoint { get; init; } = "";

    public string[] RoutePeerEndpoints => RoutePeerEndpoint.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    public Dictionary<string, long> ProcessIds(IReadOnlyList<ScenarioClientRole> clientRoles)
    {
        var result = new Dictionary<string, long>(StringComparer.Ordinal)
        {
            [$"client:{clientRoles[0].Name}"] = Process.GetCurrentProcess().Id,
        };
        foreach (var entry in ServerProcessIds.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var parts = entry.Split('=', 2, StringSplitOptions.TrimEntries);
            if (parts.Length != 2 || string.IsNullOrWhiteSpace(parts[0]) || string.IsNullOrWhiteSpace(parts[1]))
            {
                throw new InvalidOperationException("server process id entry must use role=pid.");
            }

            result[parts[0]] = long.Parse(parts[1]);
        }

        return result;
    }

    public static ScenarioOptions Parse(IReadOnlyList<string> args)
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

        var workDir = Required(values, "work-dir");
        return new ScenarioOptions
        {
            WorkDir = workDir,
            ScenarioFile = values.GetValueOrDefault("scenario", ""),
            ZLinkEndpoint = Required(values, "zlink-endpoint"),
            HttpEndpoint = Required(values, "http-endpoint"),
            Channel = values.GetValueOrDefault("channel", "scenario-api"),
            Mode = values.GetValueOrDefault("mode", "channel"),
            RoutePeerEndpoint = values.GetValueOrDefault("route-peer-endpoints", ""),
            ReadyFile = values.GetValueOrDefault("ready-file", Path.Combine(workDir, "server.ready")),
            ReportFile = values.GetValueOrDefault("report-file", Path.Combine(workDir, "report.json")),
            LogDir = values.GetValueOrDefault("log-dir", Path.Combine(workDir, "logs")),
            ServerName = values.GetValueOrDefault("server-name", "api-a"),
            ExecutedScriptPath = values.GetValueOrDefault("executed-script", ""),
            ServerProcessIds = values.GetValueOrDefault("server-process-ids", ""),
        };
    }

    private static string Required(Dictionary<string, string> values, string name)
    {
        if (!values.TryGetValue(name, out var value) || string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException($"--{name} is required.");
        }

        return value;
    }
}

internal static class ScenarioJson
{
    public static JsonSerializerOptions Options { get; } = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
    };
}
