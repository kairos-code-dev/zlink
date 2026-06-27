using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using SpotService.Server.Play.Endpoints;
using SpotService.Server.Play.Handlers;
using SpotService.Server.Play.Spots;

namespace SpotService.Server.Play.Spots;


internal sealed class MultiNodeCreateSpotAHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<MultiNodeCreateSpotReq, MultiNodeCreateSpotReply>
{
    public async ValueTask<MultiNodeCreateSpotReply> HandleAsync(
        MultiNodeCreateSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var result = await spots.GetOrCreateAsync<MultiNodeSpotA>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        var state = await MultiNodeScenario.RequestStateWithRetryAsync(
            routes,
            SpotServiceNames.MultiRouteChannelA,
            request.SpotRid,
            request.Delta,
            cancellationToken);
        evidence.Add($"multi-create-spot|node={SpotServiceNames.MultiSpotNodeA}|spot={result.SpotRid}|state={result.State}");
        return new MultiNodeCreateSpotReply(
            result.SpotRid.ToString(),
            SpotServiceNames.MultiSpotNodeA,
            result.State.ToString(),
            state.Value);
    }
}

internal sealed class MultiNodeCreateSpotBHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<MultiNodeCreateSpotReq, MultiNodeCreateSpotReply>
{
    public async ValueTask<MultiNodeCreateSpotReply> HandleAsync(
        MultiNodeCreateSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var result = await spots.GetOrCreateAsync<MultiNodeSpotB>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        var state = await MultiNodeScenario.RequestStateWithRetryAsync(
            routes,
            SpotServiceNames.MultiRouteChannelB,
            request.SpotRid,
            request.Delta,
            cancellationToken);
        evidence.Add($"multi-create-spot|node={SpotServiceNames.MultiSpotNodeB}|spot={result.SpotRid}|state={result.State}");
        return new MultiNodeCreateSpotReply(
            result.SpotRid.ToString(),
            SpotServiceNames.MultiSpotNodeB,
            result.State.ToString(),
            state.Value);
    }
}

internal static class MultiNodeScenario
{
    public static async Task<StateReply> RequestStateWithRetryAsync(
        IZLinkRouteClient routes,
        string channelName,
        string spotRid,
        int delta,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await routes.Request(
                        channelName,
                        RoutingId.From(spotRid),
                        new StateReq("add", delta))
                    .PacketName("StateReq")
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StateReply>(cancellationToken);
            }
            catch (TimeoutException ex)
            {
                last = ex;
            }
            catch (ZLinkFrameworkException ex) when (ex.InnerException is ZlinkRequestException or ZlinkSubmitException)
            {
                last = ex;
            }
            catch (Exception ex) when (ex is ZlinkRequestException or ZlinkSubmitException)
            {
                last = ex;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
        }

        throw new TimeoutException($"Timed out waiting for multi-node spot route '{spotRid}' on '{channelName}'.", last);
    }
}

internal sealed class MultiNodeSpotA(IZLinkSpotContext context, EvidenceStore evidence) : IZLinkSpot
{
    private int _value;

    public IZLinkSpotContext Context { get; } = context;

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"multi-spot-initialize|node={SpotServiceNames.MultiSpotNodeA}|spot={Context.SpotRid}");
        return ValueTask.CompletedTask;
    }

    public int Add(int delta)
    {
        _value += delta;
        return _value;
    }
}

internal sealed class MultiNodeSpotB(IZLinkSpotContext context, EvidenceStore evidence) : IZLinkSpot
{
    private int _value;

    public IZLinkSpotContext Context { get; } = context;

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"multi-spot-initialize|node={SpotServiceNames.MultiSpotNodeB}|spot={Context.SpotRid}");
        return ValueTask.CompletedTask;
    }

    public int Add(int delta)
    {
        _value += delta;
        return _value;
    }
}

internal sealed class ScenarioStage(ScenarioUserSpot spot)
{
    public StateReply Apply(StageProbeReq request, EvidenceStore evidence)
    {
        var value = spot.Add(request.Delta);
        evidence.Add(
            $"stage-request|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|marker={request.Marker}|value={value}");
        return new StateReply(
            spot.Context.SpotRid.ToString(),
            spot.Context.NodeRid.ToString(),
            value);
    }

    public async ValueTask StartTimerAsync(
        StageTimerStartCommand command,
        CancellationToken cancellationToken)
    {
        await spot.Context.AddTimer<StageTimerHandler>(
            command.Name,
            TimeSpan.FromMilliseconds(command.PeriodMs),
            cancellationToken: cancellationToken);
    }
}
