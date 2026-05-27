using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;

public abstract partial class SpotTestSupport
{
    public sealed class SpotCreatePayloadRecorder
    {
        private readonly object _gate = new();
        private TaskCompletionSource? _entered;
        private TaskCompletionSource? _release;

        public ConcurrentBag<IReadOnlyList<string>> Payloads { get; } = [];

        public void BlockCreate()
        {
            lock (_gate)
            {
                _entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            }
        }

        public Task WaitCreateEnteredAsync()
        {
            lock (_gate)
            {
                return _entered?.Task ?? Task.CompletedTask;
            }
        }

        public void ReleaseCreate()
        {
            lock (_gate)
            {
                _release?.TrySetResult();
            }
        }

        public async ValueTask RecordAsync(
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken)
        {
            Task? release;
            lock (_gate)
            {
                _entered?.TrySetResult();
                release = _release?.Task;
            }

            if (release is not null)
            {
                await release.WaitAsync(cancellationToken);
            }

            Payloads.Add(createParts.Select(static part => part.GetString()).ToArray());
        }
    }

    public sealed class SpotLifecycleRecorder
    {
        public ConcurrentBag<string> LocalEvents { get; } = [];

        public ConcurrentBag<string> ExternalEvents { get; } = [];

        private int _tickCount;

        public int TickCount => Volatile.Read(ref _tickCount);

        public void RecordTick()
        {
            Interlocked.Increment(ref _tickCount);
        }
    }

    public sealed class SpotEventsRecorder
    {
        private readonly ConcurrentDictionary<global::Systems.Zlink.RoutingId, string> _scopes = [];
        private readonly ConcurrentBag<global::Systems.Zlink.RoutingId> _closing = [];

        public ConcurrentDictionary<global::Systems.Zlink.RoutingId, string> Initialized => _scopes;

        public ConcurrentBag<global::Systems.Zlink.RoutingId> Closing => _closing;

        public void RecordInitialized(global::Systems.Zlink.RoutingId spotRid, string scopeId)
        {
            _scopes[spotRid] = scopeId;
        }

        public void RecordClosing(global::Systems.Zlink.RoutingId spotRid)
        {
            _closing.Add(spotRid);
        }

        public string? ScopeId(global::Systems.Zlink.RoutingId spotRid)
        {
            return _scopes.TryGetValue(spotRid, out var scopeId) ? scopeId : null;
        }
    }

    public sealed class PublishingStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _ = await Context.AddTimer<SpotHeartbeatTimerHandler>(
                "heartbeat",
                TimeSpan.FromMilliseconds(250),
                cancellationToken: cancellationToken);
        }
    }

    public sealed class TimerFailureProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (@event.Event == ZLinkSpotEventKind.TimerHandlerFailed)
            {
                _completion.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _completion.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class LocalSubscriberStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddSubscribe<LocalStageEventHandler>("stage.local");
        }
    }

    public sealed class ExternalSubscriberStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddSubscribe<ExternalStageEventHandler>("stage.external");
        }
    }

    public sealed record LocalStageEvent(string SpotRid);

    public sealed record ExternalStageEvent(string Value);

    public sealed class SpotHeartbeatTimerHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotTimerHandler<PublishingStageSpot>
    {
        public async ValueTask HandleAsync(
            PublishingStageSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken)
        {
            _ = tick;
            recorder.RecordTick();
            await spot.Context.Outbound.Publish("stage.local", new LocalStageEvent(spot.Context.SpotRid.ToString()))
                .Submit(cancellationToken);
        }
    }

    public sealed class LocalStageEventHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotSubscriptionHandler<LocalSubscriberStageSpot, LocalStageEvent>
    {
        public ValueTask HandleAsync(
            LocalSubscriberStageSpot spot,
            LocalStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.LocalEvents.Add(message.SpotRid);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ExternalStageEventHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotSubscriptionHandler<ExternalSubscriberStageSpot, ExternalStageEvent>
    {
        public ValueTask HandleAsync(
            ExternalSubscriberStageSpot spot,
            ExternalStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.ExternalEvents.Add(message.Value);
            return ValueTask.CompletedTask;
        }
    }
}
