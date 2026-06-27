using System.Collections.Concurrent;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Registry;

namespace RegistryMessaging.Server.Provider;

internal static class ProviderHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "provider");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint
                ?? throw new InvalidOperationException("--registry-router-endpoint is required."));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            if (!string.IsNullOrWhiteSpace(options.ChannelEndpoint))
            {
                var clientServer = framework.AddClientServerChannel("profile")
                    .EnableServer(options.ChannelEndpoint)
                    .EnableClient()
                    .SetRoutingId(RoutingId.From(options.Rid));
                clientServer.ConfigureServerSocket().Weight = options.Weight;
                clientServer.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
                clientServer.AddRequestHandler<PayloadRequestHandler, PayloadRequest, PayloadReply>("PayloadRequest");
                clientServer.AddSendHandler<ProfileCommandHandler, ProfileCommand>("ProfileCommand");
            }
            if (!string.IsNullOrWhiteSpace(options.ManualClientEndpoint))
            {
                framework.AddClientServerChannel("profile.manual")
                    .EnableClient(options.ManualClientEndpoint);
            }

            if (!string.IsNullOrWhiteSpace(options.WorkflowEndpoint))
            {
                var workflow = framework.AddClientServerChannel("workflow")
                    .EnableServer(options.WorkflowEndpoint)
                    .EnableClient()
                    .SetRoutingId(RoutingId.From(options.Rid));
                workflow.ConfigureServerSocket().Weight = options.Weight;
                workflow.AddRequestHandler<WorkflowRequestHandler, WorkflowRequest, WorkflowReply>("WorkflowRequest");
            }

            if (!string.IsNullOrWhiteSpace(options.RouteEndpoint))
            {
                var route = framework.AddRouteMesh("profile.route")
                    .EnableServer(options.RouteEndpoint)
                    .SetRoutingId(RoutingId.From(options.Rid));
                foreach (var peer in options.RoutePeers)
                {
                    route.EnableClient(peer);
                }

                route.AddRequestHandler<RoutePingHandler, ScenarioRoutePing, ScenarioRoutePong>("ScenarioRoutePing");
            }
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/profile/request", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, "profile", request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/manual", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, "profile.manual", request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/command", async (
            ProfileCommand command,
            IZLinkChannelClient channel) =>
        {
            await SendProfileWithRetryAsync(channel, "profile", command);
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/workflow/request", async (
            WorkflowRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestWorkflowWithRetryAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/route/request", async (
            ScenarioRoutePing request,
            IZLinkRouteClient route) =>
        {
            var reply = await RequestRouteWithRetryAsync(route, RoutingId.From("api-b"), request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/route/missing", async (
            ScenarioRoutePing request,
            IZLinkRouteClient route) =>
        {
            var failed = false;
            try
            {
                await route.Request("profile.route", RoutingId.From("missing-rid"), request)
                    .PacketName("ScenarioRoutePing")
                    .Timeout(TimeSpan.FromMilliseconds(300))
                    .Async<ScenarioRoutePong>();
            }
            catch (Exception)
            {
                failed = true;
            }

            return Results.Ok(new RouteMissingResult(failed));
        });
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/evidence/wait", async (
            EvidenceWaitRequest request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                line => line.Contains(request.Contains, StringComparison.Ordinal),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    static async Task<ProfileReply> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        string channelName,
        ProfileRequest request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel(channelName, request)
                    .PacketName("ProfileRequest")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ProfileReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for profile request channel route.", last);
    }

    static async Task SendProfileWithRetryAsync(
        IZLinkChannelClient channel,
        string channelName,
        ProfileCommand command)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                await channel.SendToChannel(channelName, command)
                    .PacketName("ProfileCommand")
                    .Async();
                return;
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for profile send channel route.", last);
    }

    static bool IsRetriableRequestStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;

    static async Task<WorkflowReply> RequestWorkflowWithRetryAsync(
        IZLinkChannelClient channel,
        WorkflowRequest request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("workflow", request)
                    .PacketName("WorkflowRequest")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<WorkflowReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for workflow request channel route.", last);
    }

    static async Task<ScenarioRoutePong> RequestRouteWithRetryAsync(
        IZLinkRouteClient route,
        RoutingId target,
        ScenarioRoutePing request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await route.Request("profile.route", target, request)
                    .PacketName("ScenarioRoutePing")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ScenarioRoutePong>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for route mesh target.", last);
    }
}

internal sealed class ProfileRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public async ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (request.Value == "slow")
        {
            await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        }

        evidence.Add($"profile-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return new ProfileReply($"profile:{request.Value}", evidence.Rid);
    }
}

internal sealed class ProfileCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<ProfileCommand>
{
    public async ValueTask HandleAsync(
        ProfileCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (command.CommandId.StartsWith("rm-c9-slow-", StringComparison.Ordinal))
        {
            await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        }

        evidence.Add($"profile-command|rid={evidence.Rid}|command={command.CommandId}|packet={context.PacketName}");
    }
}

internal sealed class PayloadRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<PayloadRequest, PayloadReply>
{
    public ValueTask<PayloadReply> HandleAsync(
        PayloadRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(request.Payload)));
        evidence.Add(
            $"payload-request|rid={evidence.Rid}|marker={request.Marker}"
            + $"|length={request.Payload.Length}|sha256={hash}|packet={context.PacketName}");
        return ValueTask.FromResult(new PayloadReply(request.Marker, request.Payload.Length, hash));
    }
}

internal sealed class WorkflowRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<WorkflowRequest, WorkflowReply>
{
    public ValueTask<WorkflowReply> HandleAsync(
        WorkflowRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"workflow-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return ValueTask.FromResult(new WorkflowReply($"workflow:{request.Value}", evidence.Rid));
    }
}

internal sealed class RoutePingHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<ScenarioRoutePing, ScenarioRoutePong>
{
    public ValueTask<ScenarioRoutePong> HandleAsync(
        ScenarioRoutePing request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var source = context.SourceNodeRid.ToString();
        evidence.Add($"route-request|rid={evidence.Rid}|source={source}|value={request.Value}");
        return ValueTask.FromResult(new ScenarioRoutePong($"route:{request.Value}", evidence.Rid, source));
    }
}

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != ZLinkMessageFlowOutcome.Error)
        {
            return ValueTask.CompletedTask;
        }

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;
    private readonly SemaphoreSlim _signal = new(0);

    public EvidenceStore(string? filePath)
    {
        _filePath = filePath;
        Rid = Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node";
        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_filePath)!);
            File.WriteAllText(_filePath, string.Empty);
        }
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        _signal.Release();
        if (string.IsNullOrWhiteSpace(_filePath))
        {
            return;
        }

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot() => _entries.ToArray();

    public async Task<string[]> WaitUntilAsync(
        Func<string, bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = Snapshot();
            if (snapshot.Any(predicate))
            {
                return snapshot;
            }

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || !await _signal.WaitAsync(remaining, cancellationToken))
            {
                return Snapshot();
            }
        }
    }

    public void Clear()
    {
        while (_entries.TryDequeue(out _))
        {
        }

        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            lock (_fileGate)
            {
                File.WriteAllText(_filePath, string.Empty);
            }
        }
    }
}

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? ManualClientEndpoint,
    string? WorkflowEndpoint,
    string? RouteEndpoint,
    int Weight,
    IReadOnlyList<string> RoutePeers)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var routePeers = new List<string>();

        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            var value = args[++i];
            if (key == "--route-peer")
            {
                routePeers.Add(value);
            }
            else
            {
                values[key[2..]] = value;
            }
        }

        var rid = values.GetValueOrDefault("rid", "node");
        Environment.SetEnvironmentVariable("ZLINK_E2E_RID", rid);
        return new ServerOptions(
            defaultRole,
            values.GetValueOrDefault("http-url", "http://127.0.0.1:0"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            values.GetValueOrDefault("evidence-file"),
            rid,
            values.GetValueOrDefault("registry-pub-endpoint"),
            values.GetValueOrDefault("registry-router-endpoint"),
            values.GetValueOrDefault("channel-endpoint"),
            values.GetValueOrDefault("manual-client-endpoint"),
            values.GetValueOrDefault("workflow-endpoint"),
            values.GetValueOrDefault("route-endpoint"),
            int.TryParse(values.GetValueOrDefault("weight"), out var weight) ? weight : 100,
            routePeers);
    }
}
