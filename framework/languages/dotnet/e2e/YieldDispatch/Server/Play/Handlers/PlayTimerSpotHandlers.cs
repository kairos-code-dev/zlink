using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using YieldDispatch.Server.Play.Handlers;
using YieldDispatch.Server.Play.Spots;

namespace YieldDispatch.Server.Play.Handlers;

[ZLinkSpotRequestHandler("TimerStartReq")]
internal sealed class TimerStartHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, TimerStartReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        TimerStartReq request,
        CancellationToken cancellationToken)
    {
        var state = new YieldTimerState(
            request.RequestId,
            request.TimerName,
            request.Mode,
            request.DelayMs);
        if (!spot.TryAddTimerState(state))
        {
            evidence.Add(
                $"timer-start-duplicate-ignored|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={request.RequestId}|timer={request.TimerName}|mode={request.Mode}");
            return YieldReplies.Reply("YD-C", request.RequestId, spot, "timer-started");
        }

        state.Timer = await spot.Context.AddTimer<YieldTimerHandler>(
            request.TimerName,
            TimeSpan.FromMilliseconds(request.PeriodMs),
            new ZLinkTimerOptions { OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick },
            cancellationToken);
        evidence.Add(
            $"timer-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|timer={request.TimerName}|mode={request.Mode}");
        return YieldReplies.Reply("YD-C", request.RequestId, spot, "timer-started");
    }
}

[ZLinkSpotPacketHandler("TimerStartCommand")]
internal sealed class TimerStartCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, TimerStartCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        TimerStartCommand request,
        CancellationToken cancellationToken)
    {
        var state = new YieldTimerState(
            request.RequestId,
            request.TimerName,
            request.Mode,
            request.DelayMs);
        if (!spot.TryAddTimerState(state))
        {
            evidence.Add(
                $"timer-start-duplicate-ignored|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={request.RequestId}|timer={request.TimerName}|mode={request.Mode}");
            return;
        }

        state.Timer = await spot.Context.AddTimer<YieldTimerHandler>(
            request.TimerName,
            TimeSpan.FromMilliseconds(request.PeriodMs),
            new ZLinkTimerOptions { OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick },
            cancellationToken);
        evidence.Add(
            $"timer-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|timer={request.TimerName}|mode={request.Mode}");
    }
}

[ZLinkSpotRequestHandler("TimerStopReq")]
internal sealed class TimerStopHandler
    : IZLinkSpotRequestHandler<YieldProbeSpot, TimerStopReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        TimerStopReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await spot.StopScenarioTimersAsync(request.RequestId);
        return YieldReplies.Reply("YD-C", request.RequestId, spot, "timer-stopped");
    }
}

[ZLinkSpotPacketHandler("TimerStopCommand")]
internal sealed class TimerStopCommandHandler
    : IZLinkSpotPacketHandler<YieldProbeSpot, TimerStopCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        TimerStopCommand request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await spot.StopScenarioTimersAsync(request.RequestId);
    }
}

internal sealed class YieldTimerHandler(EvidenceStore evidence)
    : IZLinkSpotTimerHandler<YieldProbeSpot>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        var state = spot.FindTimerState(tick.Name);
        if (state is null)
        {
            return;
        }

        var tickNumber = state.NextTick();
        if (string.Equals(state.Mode, "fast", StringComparison.Ordinal))
        {
            evidence.Add(
                $"timer-fast-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            evidence.Add(
                $"timer-fast-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            return;
        }

        if (tickNumber == 1
            && (string.Equals(state.Mode, "yield-on-first", StringComparison.Ordinal)
                || string.Equals(state.Mode, "yield-then-next", StringComparison.Ordinal)))
        {
            evidence.Add(
                $"timer-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(state.RequestId, state.DelayMs, state.TimerName))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add(
                $"timer-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            await call.Yield<DelayReply>(cancellationToken);
            evidence.Add(
                $"timer-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            evidence.Add(
                $"timer-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            return;
        }

        if (string.Equals(state.Mode, "yield-then-next", StringComparison.Ordinal) && tickNumber == 2)
        {
            evidence.Add(
                $"timer-next-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            evidence.Add(
                $"timer-next-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
        }
    }
}
