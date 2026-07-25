using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkMessageFlowRuntimeService(
    ZLinkDispatchOptionsModel options) : IZLinkMessageFlowRuntime
{
    private readonly object _gate = new();
    private readonly HashSet<Subscription> _subscriptions =
        new(ReferenceEqualityComparer.Instance);

    public ZLinkRuntimeMessageFlowMode Mode
    {
        get => Map(options.Diagnostics.EffectiveMessageFlow);
        set
        {
            var mapped = Map(value);
            if (options.Diagnostics.LiveMode is { } cell)
                cell.Mode = mapped;
            else
                options.Diagnostics.MessageFlow = mapped;
        }
    }

    internal bool HasSubscribers
    {
        get
        {
            lock (_gate) return _subscriptions.Count > 0;
        }
    }

    internal void Publish(ZLinkRuntimeMessageFlowEvent flow)
    {
        Subscription[] subscriptions;
        lock (_gate) subscriptions = [.. _subscriptions];
        foreach (var subscription in subscriptions)
            if (!subscription.Writer.TryWrite(flow))
                ZLinkRuntimeMetrics.RecordObserverOverflow(flow.Phase ?? flow.EventId);
    }

    public async IAsyncEnumerable<ZLinkRuntimeMessageFlowEvent> ObserveAsync(
        int capacity = 1024,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));

        var subscription = new Subscription(capacity);
        lock (_gate) _subscriptions.Add(subscription);
        try
        {
            while (await subscription.Reader.WaitToReadAsync(cancellationToken)
                       .ConfigureAwait(false))
                while (subscription.Reader.TryRead(out var flow))
                    yield return flow;
        }
        finally
        {
            lock (_gate) _subscriptions.Remove(subscription);
            subscription.Writer.TryComplete();
            while (subscription.Reader.TryRead(out _))
            {
            }
        }
    }

    private static ZLinkRuntimeMessageFlowMode Map(ZLinkMessageFlowLogMode mode) =>
        mode switch
        {
            ZLinkMessageFlowLogMode.Off => ZLinkRuntimeMessageFlowMode.Off,
            ZLinkMessageFlowLogMode.ErrorsOnly => ZLinkRuntimeMessageFlowMode.ErrorsOnly,
            ZLinkMessageFlowLogMode.KeyTransitions => ZLinkRuntimeMessageFlowMode.KeyTransitions,
            ZLinkMessageFlowLogMode.Verbose => ZLinkRuntimeMessageFlowMode.Verbose,
            ZLinkMessageFlowLogMode.Diagnostic => ZLinkRuntimeMessageFlowMode.Verbose,
            _ => throw new ArgumentOutOfRangeException(nameof(mode))
        };

    private static ZLinkMessageFlowLogMode Map(ZLinkRuntimeMessageFlowMode mode) =>
        mode switch
        {
            ZLinkRuntimeMessageFlowMode.Off => ZLinkMessageFlowLogMode.Off,
            ZLinkRuntimeMessageFlowMode.ErrorsOnly => ZLinkMessageFlowLogMode.ErrorsOnly,
            ZLinkRuntimeMessageFlowMode.KeyTransitions => ZLinkMessageFlowLogMode.KeyTransitions,
            ZLinkRuntimeMessageFlowMode.Verbose => ZLinkMessageFlowLogMode.Verbose,
            _ => throw new ArgumentOutOfRangeException(nameof(mode))
        };

    private sealed class Subscription
    {
        private readonly Channel<ZLinkRuntimeMessageFlowEvent> _channel;

        internal Subscription(int capacity)
        {
            _channel = Channel.CreateBounded<ZLinkRuntimeMessageFlowEvent>(
                new BoundedChannelOptions(capacity)
                {
                    FullMode = BoundedChannelFullMode.DropWrite,
                    SingleReader = true,
                    SingleWriter = false
                },
                static dropped => ZLinkRuntimeMetrics.RecordObserverOverflow(
                    dropped.Phase ?? dropped.EventId));
        }

        internal ChannelReader<ZLinkRuntimeMessageFlowEvent> Reader =>
            _channel.Reader;

        internal ChannelWriter<ZLinkRuntimeMessageFlowEvent> Writer =>
            _channel.Writer;
    }
}

internal static class ZLinkRuntimeMessageFlowProjection
{
    internal static bool TryProject(
        ZLinkMessageFlowEvent source,
        out ZLinkRuntimeMessageFlowEvent projected)
    {
        if (source.MessageKind == ZLinkDispatchMessageKind.Publish)
        {
            projected = null!;
            return false;
        }

        var dispatchError = source.Outcome == ZLinkMessageFlowOutcome.Error;
        var (surface, routeKind) = Surface(source.Surface);
        projected = new ZLinkRuntimeMessageFlowEvent(
            dispatchError ? "zlink.dispatch_error" : "zlink.message_flow",
            DateTimeOffset.UtcNow,
            dispatchError ? null : Phase(source.Outcome),
            surface,
            MessageKind(source.MessageKind),
            dispatchError ? "failed" : Outcome(source.Outcome),
            Reason(source),
            dispatchError ? Action(source.ErrorAction) : null,
            MeshName: null,
            source.ChannelName,
            routeKind,
            RoutingId(source.SourceRid),
            RoutingId(source.PeerRid),
            ServerRid: null,
            source.PacketName,
            source.Topic,
            source.SpotId,
            InstanceSpotType: null,
            ActivationState: null,
            source.ActorId,
            source.CorrelationId,
            string.IsNullOrEmpty(source.FlowId) ? null : source.FlowId,
            source.FlowOrigin?.ToString().ToLowerInvariant(),
            source.MessageSize,
            DurationSeconds: null);
        return true;
    }

    private static (string Surface, string? RouteKind) Surface(
        ZLinkDispatchErrorSurface surface) =>
        surface switch
        {
            ZLinkDispatchErrorSurface.Channel => ("channel", "client_server"),
            ZLinkDispatchErrorSurface.RouteMeshChannel => ("channel", "route_mesh"),
            ZLinkDispatchErrorSurface.SpotRoute => ("spot", null),
            ZLinkDispatchErrorSurface.SpotSubscription => ("spot", null),
            ZLinkDispatchErrorSurface.SpotActor => ("actor", null),
            ZLinkDispatchErrorSurface.StreamSession => ("stream", null),
            _ => throw new ArgumentOutOfRangeException(nameof(surface))
        };

    private static string MessageKind(ZLinkDispatchMessageKind kind) =>
        kind switch
        {
            ZLinkDispatchMessageKind.Request or
                ZLinkDispatchMessageKind.ActorRequest => "request",
            ZLinkDispatchMessageKind.Send or
                ZLinkDispatchMessageKind.ActorSend => "send",
            ZLinkDispatchMessageKind.Response => "response",
            ZLinkDispatchMessageKind.Error => "error",
            _ => "control"
        };

    private static string Phase(ZLinkMessageFlowOutcome outcome) =>
        outcome switch
        {
            ZLinkMessageFlowOutcome.Received => "received",
            ZLinkMessageFlowOutcome.Dispatched => "dispatched",
            ZLinkMessageFlowOutcome.Replied => "replied",
            ZLinkMessageFlowOutcome.Dropped => "dropped",
            ZLinkMessageFlowOutcome.Sent => "sent",
            ZLinkMessageFlowOutcome.ReplyReceived => "reply_received",
            _ => throw new ArgumentOutOfRangeException(nameof(outcome))
        };

    private static string Outcome(ZLinkMessageFlowOutcome outcome) =>
        outcome == ZLinkMessageFlowOutcome.Dropped ? "dropped" : "succeeded";

    private static string? Reason(ZLinkMessageFlowEvent source) =>
        source.ErrorReason switch
        {
            ZLinkDispatchErrorReason.HandlerMissing => "no_handler",
            ZLinkDispatchErrorReason.PayloadDecodeFailed => "decode_error",
            ZLinkDispatchErrorReason.HandlerException => "handler_exception",
            ZLinkDispatchErrorReason.InvalidFrame => "invalid_frame",
            ZLinkDispatchErrorReason.ReplyPathMissing => "reply_path_missing",
            ZLinkDispatchErrorReason.UnexpectedReply => "unexpected_reply",
            _ => source.Outcome == ZLinkMessageFlowOutcome.Dropped
                ? "target_closed"
                : null
        };

    private static string? Action(ZLinkDispatchErrorAction? action) =>
        action switch
        {
            ZLinkDispatchErrorAction.ReplyError => "reply_error",
            ZLinkDispatchErrorAction.Drop => "drop",
            ZLinkDispatchErrorAction.FailCaller => "fail_caller",
            _ => null
        };

    private static RoutingId? RoutingId(string? value) =>
        string.IsNullOrEmpty(value) ? null : Systems.Zlink.RoutingId.From(value);
}
