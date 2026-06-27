using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using YieldDispatch.Server.Play.Handlers;
using YieldDispatch.Server.Play.Spots;

namespace YieldDispatch.Server.Play.Spots;

internal sealed class YieldProbeSpot(
    IZLinkSpotContext context,
    EvidenceStore evidence) : IZLinkSpot<YieldActor>
{
    private readonly object _timerGate = new();
    private readonly Dictionary<string, YieldTimerState> _timers = new(StringComparer.Ordinal);

    public IZLinkSpotContext Context { get; } = context;

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        YieldActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!request.IsEmpty)
        {
            var delay = request.Decode<DelayReq>();
            if (delay.DelayMs > 0)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(delay.DelayMs), cancellationToken);
            }
        }

        evidence.Add($"actor-joined|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ZLinkSpotActorJoinResult.Accept(request);
    }

    public bool TryAddTimerState(YieldTimerState state)
    {
        lock (_timerGate)
        {
            if (_timers.ContainsKey(state.TimerName))
            {
                return false;
            }

            _timers[state.TimerName] = state;
            return true;
        }
    }

    public YieldTimerState? FindTimerState(string timerName)
    {
        lock (_timerGate)
        {
            return _timers.TryGetValue(timerName, out var state) ? state : null;
        }
    }

    public async ValueTask StopScenarioTimersAsync(string requestId)
    {
        List<YieldTimerState> states;
        lock (_timerGate)
        {
            states = _timers.Values
                .Where(state => string.Equals(state.RequestId, requestId, StringComparison.Ordinal))
                .ToList();
            foreach (var state in states)
            {
                _timers.Remove(state.TimerName);
            }
        }

        foreach (var state in states)
        {
            if (state.Timer is not null)
            {
                await state.Timer.CancelAsync();
            }
        }
    }
}

internal sealed class YieldTimerState(
    string requestId,
    string timerName,
    string mode,
    int delayMs)
{
    private int _tickCount;

    public string RequestId { get; } = requestId;

    public string TimerName { get; } = timerName;

    public string Mode { get; } = mode;

    public int DelayMs { get; } = delayMs;

    public IZLinkTimer? Timer { get; set; }

    public int NextTick() => Interlocked.Increment(ref _tickCount);
}
