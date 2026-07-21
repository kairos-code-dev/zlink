using System.Security.Cryptography;
using System.Text;
using LocationMessaging.Server.Provider.Infrastructure;
using LocationMessaging.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;

namespace LocationMessaging.Server.Provider.Handlers;

internal sealed class ProfileRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ProfileReq, ProfileRes>
{
    public async ValueTask<ProfileRes> HandleAsync(
        ProfileReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (request.Value == "slow") await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        if (request.Value.StartsWith("rm-b3-transition-", StringComparison.Ordinal))
        {
            evidence.Add($"profile-request-start|rid={evidence.Rid}|value={request.Value}");
            await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        }

        evidence.Add($"profile-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return new ProfileRes($"profile:{request.Value}", evidence.Rid);
    }
}

internal sealed class ProfileCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<ProfileMsg>
{
    public async ValueTask HandleAsync(
        ProfileMsg command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (command.CommandId.StartsWith("rm-c9-slow-", StringComparison.Ordinal))
            await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);

        evidence.Add($"profile-command|rid={evidence.Rid}|command={command.CommandId}|packet={context.PacketName}");
    }
}

internal sealed class PayloadRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<PayloadReq, PayloadRes>
{
    public ValueTask<PayloadRes> HandleAsync(
        PayloadReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(request.Payload)));
        evidence.Add(
            $"payload-request|rid={evidence.Rid}|marker={request.Marker}"
            + $"|length={request.Payload.Length}|sha256={hash}|packet={context.PacketName}");
        return ValueTask.FromResult(new PayloadRes(request.Marker, request.Payload.Length, hash));
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
        if (flow.Outcome is not (ZLinkMessageFlowOutcome.Error or ZLinkMessageFlowOutcome.Dropped))
            return ValueTask.CompletedTask;

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

internal sealed class ProfileMeshEventObserver(
    EvidenceStore evidence,
    Zlink.Framework.Contracts.Configuration.IZLinkRouteMeshRuntime meshRuntime)
    : IZLinkRuntimeEventHandler<Zlink.Framework.Contracts.Configuration.ZLinkMeshRuntimeEvent>
{
    private static readonly System.Collections.Concurrent.ConcurrentDictionary<string, string>
        LastKnownEndpoints = new(StringComparer.Ordinal);

    public ValueTask HandleAsync(
        Zlink.Framework.Contracts.Configuration.ZLinkMeshRuntimeEvent @event,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var peer = @event.PeerRid?.ToString() ?? string.Empty;
        var endpoint = string.Empty;
        if (peer.Length > 0)
        {
            try
            {
                endpoint = meshRuntime.Snapshot(@event.MeshName).Peers
                    .FirstOrDefault(candidate => candidate.Rid.ToString() == peer)?.Endpoint
                    ?? string.Empty;
            }
            catch (Exception)
            {
                // A final event can race mesh shutdown; keep the last endpoint
                // observed while this peer was ready.
            }

            if (endpoint.Length > 0) LastKnownEndpoints[peer] = endpoint;
            else LastKnownEndpoints.TryGetValue(peer, out endpoint!);
        }

        var kind = @event.Reason switch
        {
            "ready" => "ConnectionReady",
            "disconnected" => "Disconnected",
            _ => @event.Reason ?? @event.Identifier
        };
        evidence.Add(
            $"monitor-mesh|source={@event.MeshName}|kind={kind}"
            + $"|remote={endpoint}|routing={peer}|sequence={@event.Sequence}");
        return ValueTask.CompletedTask;
    }
}
