using System.Collections.Concurrent;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;

namespace Zlink.Framework.ScenarioE2E;

// One shared server program backs every channel and dispatch-error scenario.
// The runner brings up one or more copies of it (round-robin needs three, the
// route mesh needs two peers) and the client scenario talks to it.
internal static class ScenarioServer
{
    public static async Task RunAsync(ScenarioServerOptions options)
    {
        Directory.CreateDirectory(options.WorkDir);

        var builder = WebApplication.CreateBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls(options.HttpEndpoint);
        builder.Services.AddSingleton(new ScenarioEvidence(options.ServerName));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch().SetMessageDispatchErrorObserver<ScenarioDispatchObserver>();

            if (options.Mode == "route-mesh")
            {
                var routed = framework.AddRouteMeshChannel(options.Channel);
                routed.EnableServer(options.ZLinkEndpoint);
                routed.SetRoutingId(RoutingId.From(options.ServerName));
                foreach (var peer in options.RoutePeerEndpoints)
                {
                    routed.EnableClient(peer);
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
        app.MapGet("/evidence", (ScenarioEvidence evidence) => Results.Json(evidence.Snapshot()));

        await File.WriteAllTextAsync(options.ReadyFile, "ready");
        await app.RunAsync();
    }
}

// Request handler used by the client-server channel scenarios. "slow" / "timeout"
// requests sleep long enough to trip a client timeout; "throw" forces a handler
// exception for the dispatch-error scenario. Every id is recorded as evidence.
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
        if (request.Value == "throw")
        {
            throw new InvalidOperationException("DERR-007 handler exception");
        }

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

// Records each dispatch error as a stable marker string so the client can assert
// the unregistered / handler-exception paths from the server side.
internal sealed class ScenarioDispatchObserver(ScenarioEvidence evidence) : IZLinkMessageDispatchErrorObserver
{
    public ValueTask OnDispatchErrorAsync(ZLinkMessageDispatchErrorEvent error, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var marker = new StringBuilder()
            .Append("surface=").Append(Camel(error.Surface))
            .Append(" kind=").Append(Camel(error.MessageKind))
            .Append(" reason=").Append(Camel(error.Reason))
            .Append(" action=").Append(Camel(error.Action));
        if (error.PacketName is { } packetName)
        {
            marker.Append(" packetName=").Append(packetName);
        }

        if (error.ChannelName is { } channelName)
        {
            marker.Append(" channelName=").Append(channelName);
        }

        if (error.Exception is { } exception)
        {
            marker.Append(" error=").Append(exception.Message);
        }

        evidence.DispatchErrors.Enqueue(marker.ToString());
        return ValueTask.CompletedTask;
    }

    private static string Camel(object value)
    {
        var text = value.ToString() ?? "";
        return text.Length == 0 ? text : char.ToLowerInvariant(text[0]) + text[1..];
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

    public ConcurrentQueue<string> DispatchErrors { get; } = new();

    public ScenarioEvidenceSnapshot Snapshot() => new(
        ServerId,
        RequestIds.ToArray(),
        CompletedRequestIds.ToArray(),
        CommandIds.ToArray(),
        RouteRequestIds.ToArray(),
        RouteSourceRids.ToArray(),
        DispatchErrors.ToArray());
}

internal sealed record ScenarioServerOptions(
    string WorkDir,
    string ServerName,
    string Mode,
    string Channel,
    string ZLinkEndpoint,
    string HttpEndpoint,
    string ReadyFile,
    IReadOnlyList<string> RoutePeerEndpoints);
