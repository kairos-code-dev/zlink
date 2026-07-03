using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

using Zlink.Framework.Contracts.Locations;

namespace YieldDispatch.Server.Session.Support;

internal sealed partial class YieldSession(
    IZLinkSessionContext context,
    IZLinkRouteClient routes,
    IZLinkSpotLocationResolver spots,
    EvidenceStore evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-connected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-disconnected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-error|rid={evidence.Rid}|session={Context.SessionId}|error={error.Error}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        switch (dispatch.PacketName)
        {
            case "BindYieldActorsReq":
            {
                var request = payload.Decode<BindYieldActorsReq>();
                evidence.Add(
                    $"session-bind-actors|rid={evidence.Rid}|session={Context.SessionId}|spot={request.SpotRid}");
                var result = await RequestPlayControlWithRetryAsync<BindYieldActorsRes>(
                    routes,
                    request,
                    "BindYieldActorsReq",
                    cancellationToken);
                foreach (var actor in result.Actors)
                {
                    await Context.Actors.BindAsync(
                        new ActorRef(
                            RoutingId.From(actor.NodeRid),
                            actor.ActorId,
                            actor.Generation),
                        cancellationToken);
                    evidence.Add(
                        $"session-bound-actor|rid={evidence.Rid}|session={Context.SessionId}"
                        + $"|actor={actor.ActorId}|node={actor.NodeRid}");
                }

                Context.Client.Reply(result).Submit();
                return;
            }
            case "YieldShutdownScenarioReq":
            {
                var request = payload.Decode<YieldShutdownScenarioReq>();
                evidence.Add(
                    $"session-shutdown|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}|spot={request.SpotRid}");
                var result = await RunShutdownThroughSpotRouteAsync(
                    routes,
                    spots,
                    request,
                    cancellationToken);
                Context.Client.Reply(result).Submit();
                return;
            }
            case "YieldShutdownRecoveryReq":
            {
                var request = payload.Decode<YieldShutdownRecoveryReq>();
                evidence.Add(
                    $"session-shutdown-recovery|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}|spot={request.SpotRid}");
                var result = await RunShutdownRecoveryThroughSpotRouteAsync(
                    routes,
                    spots,
                    request,
                    cancellationToken);
                Context.Client.Reply(result).Submit();
                return;
            }
            case "YieldEvidenceReq":
            {
                var request = payload.Decode<YieldEvidenceReq>();
                var result = await RequestPlayControlWithRetryAsync<YieldEvidenceRes>(
                    routes,
                    request,
                    "YieldEvidenceReq",
                    TargetOrDefault(dispatch),
                    cancellationToken);
                Context.Client.Reply(result).Submit();
                return;
            }
            case "YieldEvidenceWaitReq":
            {
                var request = payload.Decode<YieldEvidenceWaitReq>();
                var result = await RequestPlayControlWithRetryAsync<YieldEvidenceRes>(
                    routes,
                    request,
                    "YieldEvidenceWaitReq",
                    TargetOrDefault(dispatch),
                    cancellationToken);
                Context.Client.Reply(result).Submit();
                return;
            }
            case "EnsureSpotReq":
            {
                var request = payload.Decode<EnsureSpotReq>();
                var result = await RequestPlayControlWithRetryAsync<EnsureSpotRes>(
                    routes,
                    request,
                    "EnsureSpotReq",
                    TargetOrDefault(dispatch),
                    cancellationToken);
                Context.Client.Reply(result).Submit();
                return;
            }
            case "HoldReq":
            {
                await ReplySpotRequestAsync<HoldReq, YieldDispatchRes>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldReq":
            {
                await ReplySpotRequestAsync<YieldReq, YieldDispatchRes>(dispatch, payload, cancellationToken);
                return;
            }
            case "WorkerYieldReq":
            {
                await ReplySpotRequestAsync<WorkerYieldReq, YieldDispatchRes>(dispatch, payload, cancellationToken);
                return;
            }
            case "RemoteSpotYieldReq":
            {
                await ReplySpotRequestAsync<RemoteSpotYieldReq, YieldDispatchRes>(dispatch, payload,
                    cancellationToken);
                return;
            }
            case "ProbeReq":
            {
                await ReplySpotRequestAsync<ProbeReq, YieldDispatchRes>(dispatch, payload, cancellationToken);
                return;
            }
            case "HoldMsg":
            {
                await RelaySpotCommandAsync<HoldMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldMsg":
            {
                await RelaySpotCommandAsync<YieldMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "WorkerYieldMsg":
            {
                await RelaySpotCommandAsync<WorkerYieldMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "RemoteSpotYieldMsg":
            {
                await RelaySpotCommandAsync<RemoteSpotYieldMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "ProbeMsg":
            {
                await RelaySpotCommandAsync<ProbeMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldTimeoutMsg":
            {
                await RelaySpotCommandAsync<YieldTimeoutMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldCancelMsg":
            {
                await RelaySpotCommandAsync<YieldCancelMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "TimerStartMsg":
            {
                await RelaySpotCommandAsync<TimerStartMsg>(dispatch, payload, cancellationToken);
                return;
            }
            case "TimerStopMsg":
            {
                await RelaySpotCommandAsync<TimerStopMsg>(dispatch, payload, cancellationToken);
                return;
            }
            default:
            {
                var actorId = dispatch.Metadata.Find(YieldDispatchNames.ActorIdMetadata);
                var actor = string.IsNullOrWhiteSpace(actorId)
                    ? RequireSingleBoundActor()
                    : Context.Actors.Find(actorId)
                      ?? throw new InvalidOperationException($"Actor route not found: {actorId}");
                await actor.RelayAsync(payload, cancellationToken);
                return;
            }
        }
    }

    private IZLinkSessionActor RequireSingleBoundActor()
    {
        return Context.Actors.Bound.Count switch
        {
            1 => Context.Actors.Bound.Single(),
            0 => throw new InvalidOperationException("No actor is bound."),
            _ => throw new InvalidOperationException("actor-id metadata is required.")
        };
    }

    private async Task ReplySpotRequestAsync<TReq, TRes>(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var spotRid = dispatch.Metadata.Find(YieldDispatchNames.SpotRidMetadata);
        if (string.IsNullOrWhiteSpace(spotRid))
            throw new InvalidOperationException($"{YieldDispatchNames.SpotRidMetadata} metadata is required.");

        var request = payload.Decode<TReq>()
                      ?? throw new InvalidOperationException($"Failed to decode packet '{dispatch.PacketName}'.");
        var result = await RequestSpotWithRetryAsync<TRes>(
            routes,
            spots,
            spotRid,
            request,
            dispatch.PacketName,
            cancellationToken);
        Context.Client.Reply(result).Submit();
    }

    private async Task RelaySpotCommandAsync<TMsg>(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var spotRid = dispatch.Metadata.Find(YieldDispatchNames.SpotRidMetadata);
        if (string.IsNullOrWhiteSpace(spotRid))
            throw new InvalidOperationException($"{YieldDispatchNames.SpotRidMetadata} metadata is required.");

        var command = payload.Decode<TMsg>()
                      ?? throw new InvalidOperationException($"Failed to decode packet '{dispatch.PacketName}'.");
        await SendSpotWithRetryAsync(
            routes,
            spots,
            spotRid,
            command,
            dispatch.PacketName,
            cancellationToken);
    }
}