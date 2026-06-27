using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Actors;
using YieldDispatch.Server.Session;

namespace YieldDispatch.Server.Session.Support;

internal sealed partial class YieldSession(
    IZLinkSessionContext context,
    IZLinkRouteClient routes,
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
                evidence.Add($"session-bind-actors|rid={evidence.Rid}|session={Context.SessionId}|spot={request.SpotRid}");
                var result = await RequestPlayControlWithRetryAsync<BindYieldActorsReply>(
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

                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldShutdownScenarioReq":
            {
                var request = payload.Decode<YieldShutdownScenarioReq>();
                evidence.Add($"session-shutdown|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}|spot={request.SpotRid}");
                var result = await RunShutdownThroughSpotRouteAsync(
                    routes,
                    request,
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldShutdownRecoveryReq":
            {
                var request = payload.Decode<YieldShutdownRecoveryReq>();
                evidence.Add($"session-shutdown-recovery|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}|spot={request.SpotRid}");
                var result = await RunShutdownRecoveryThroughSpotRouteAsync(
                    routes,
                    request,
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldEvidenceReq":
            {
                var request = payload.Decode<YieldEvidenceReq>();
                var result = await RequestPlayControlWithRetryAsync<YieldEvidenceReply>(
                    routes,
                    request,
                    "YieldEvidenceReq",
                    TargetOrDefault(dispatch),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldEvidenceWaitReq":
            {
                var request = payload.Decode<YieldEvidenceWaitReq>();
                var result = await RequestPlayControlWithRetryAsync<YieldEvidenceReply>(
                    routes,
                    request,
                    "YieldEvidenceWaitReq",
                    TargetOrDefault(dispatch),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "EnsureSpotReq":
            {
                var request = payload.Decode<EnsureSpotReq>();
                var result = await RequestPlayControlWithRetryAsync<EnsureSpotReply>(
                    routes,
                    request,
                    "EnsureSpotReq",
                    TargetOrDefault(dispatch),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "HoldReq":
            {
                await ReplySpotRequestAsync<HoldReq, YieldDispatchReply>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldReq":
            {
                await ReplySpotRequestAsync<YieldReq, YieldDispatchReply>(dispatch, payload, cancellationToken);
                return;
            }
            case "WorkerYieldReq":
            {
                await ReplySpotRequestAsync<WorkerYieldReq, YieldDispatchReply>(dispatch, payload, cancellationToken);
                return;
            }
            case "RemoteSpotYieldReq":
            {
                await ReplySpotRequestAsync<RemoteSpotYieldReq, YieldDispatchReply>(dispatch, payload, cancellationToken);
                return;
            }
            case "ProbeReq":
            {
                await ReplySpotRequestAsync<ProbeReq, YieldDispatchReply>(dispatch, payload, cancellationToken);
                return;
            }
            case "HoldCommand":
            {
                await RelaySpotCommandAsync<HoldCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldCommand":
            {
                await RelaySpotCommandAsync<YieldCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "WorkerYieldCommand":
            {
                await RelaySpotCommandAsync<WorkerYieldCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "RemoteSpotYieldCommand":
            {
                await RelaySpotCommandAsync<RemoteSpotYieldCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "ProbeCommand":
            {
                await RelaySpotCommandAsync<ProbeCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldTimeoutCommand":
            {
                await RelaySpotCommandAsync<YieldTimeoutCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "YieldCancelCommand":
            {
                await RelaySpotCommandAsync<YieldCancelCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "TimerStartCommand":
            {
                await RelaySpotCommandAsync<TimerStartCommand>(dispatch, payload, cancellationToken);
                return;
            }
            case "TimerStopCommand":
            {
                await RelaySpotCommandAsync<TimerStopCommand>(dispatch, payload, cancellationToken);
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

    private async Task ReplySpotRequestAsync<TRequest, TReply>(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var spotRid = dispatch.Metadata.Find(YieldDispatchNames.SpotRidMetadata);
        if (string.IsNullOrWhiteSpace(spotRid))
        {
            throw new InvalidOperationException($"{YieldDispatchNames.SpotRidMetadata} metadata is required.");
        }

        var request = payload.Decode<TRequest>()
            ?? throw new InvalidOperationException($"Failed to decode packet '{dispatch.PacketName}'.");
        var result = await RequestSpotWithRetryAsync<TReply>(
            routes,
            spotRid,
            request,
            dispatch.PacketName,
            cancellationToken);
        await Context.Client.Reply(result).Async();
    }

    private async Task RelaySpotCommandAsync<TCommand>(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var spotRid = dispatch.Metadata.Find(YieldDispatchNames.SpotRidMetadata);
        if (string.IsNullOrWhiteSpace(spotRid))
        {
            throw new InvalidOperationException($"{YieldDispatchNames.SpotRidMetadata} metadata is required.");
        }

        var command = payload.Decode<TCommand>()
            ?? throw new InvalidOperationException($"Failed to decode packet '{dispatch.PacketName}'.");
        await SendSpotWithRetryAsync(
            routes,
            spotRid,
            command,
            dispatch.PacketName,
            cancellationToken);
    }

}
