using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Framework.Runtime.Protocol;
using Zlink.Framework.Runtime.Dispatch;
using Zlink.Framework.Runtime.Service;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

[Collection(RuntimeMetricsCollection.Name)]
public sealed class ExactMessageFlowRuntimeTests
{
    [Fact]
    public async Task RuntimeStreamIsBoundedAndSharesTheLiveModeCell()
    {
        var options = new ZLinkDispatchOptionsModel();
        options.Diagnostics.LiveMode = new ZLinkMessageFlowModeCell(
            ZLinkMessageFlowLogMode.ErrorsOnly);
        var runtime = new ZLinkMessageFlowRuntimeService(options);

        Assert.Equal(
            ZLinkRuntimeMessageFlowMode.ErrorsOnly,
            runtime.Mode);
        runtime.Mode = ZLinkRuntimeMessageFlowMode.Verbose;
        Assert.Equal(
            ZLinkMessageFlowLogMode.Verbose,
            options.Diagnostics.EffectiveMessageFlow);

        using var timeout = new CancellationTokenSource(
            TimeSpan.FromSeconds(2));
        await using var observer = runtime.ObserveAsync(
                capacity: 1,
                timeout.Token)
            .GetAsyncEnumerator(timeout.Token);
        var pending = observer.MoveNextAsync().AsTask();
        runtime.Publish(Projected("first"));

        Assert.True(await pending);
        Assert.Equal("first", observer.Current.PacketName);
    }

    [Fact]
    public void ProjectionUsesExactClosedValuesAndExcludesPublish()
    {
        var dispatch = new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Error,
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            ZLinkDispatchMessageKind.Request,
            PacketName: "find",
            ChannelName: "orders",
            ErrorReason: ZLinkDispatchErrorReason.HandlerMissing,
            ErrorAction: ZLinkDispatchErrorAction.ReplyError);

        Assert.True(
            ZLinkRuntimeMessageFlowProjection.TryProject(
                dispatch,
                out var projected));
        Assert.Equal("zlink.dispatch_error", projected.EventId);
        Assert.Null(projected.Phase);
        Assert.Equal("channel", projected.Surface);
        Assert.Equal("route_mesh", projected.ChannelRouteKind);
        Assert.Equal("failed", projected.Outcome);
        Assert.Equal("no_handler", projected.Reason);
        Assert.Equal("reply_error", projected.Action);

        Assert.False(
            ZLinkRuntimeMessageFlowProjection.TryProject(
                dispatch with
                {
                    MessageKind = ZLinkDispatchMessageKind.Publish
                },
                out _));
    }

    [Fact]
    public async Task ExactObserverFailureUsesSeparateSinkAndLaterEventContinues()
    {
        var observer = new FailOnceObserver();
        var sink = new RecordingRuntimeErrorSink();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkRuntimeMessageFlowMode.Off)
            .SetRuntimeMessageFlowObserver(observer)
            .SetRuntimeErrorSink(sink);
        await using var services =
            new ServiceCollection().BuildServiceProvider();
        var internalSink = new ZLinkRuntimeErrorSink();
        var runner = new ZLinkRuntimeTaskRunner(
            internalSink,
            CancellationToken.None);
        await using var pump =
            new ZLinkMessageFlowObserverPump(options, services, runner);
        var tracer = new ZLinkMessageFlowTracer(
            options,
            observerPump: pump);

        tracer.Trace(Legacy("first"));
        tracer.Trace(Legacy("second"));

        var error = await sink.Error.Task.WaitAsync(
            TimeSpan.FromSeconds(2));
        Assert.Equal("zlink.runtime_error", error.EventId);
        Assert.Equal("observer_failed", error.Kind);
        Assert.Equal("message_flow_observer", error.Source);
        Assert.True(error.Reason.Length <= 1024);
        Assert.Equal(
            "second",
            await observer.Second.Task.WaitAsync(TimeSpan.FromSeconds(2)));

        await runner.StopAsync();
        internalSink.Dispose();
    }

    [Fact]
    public async Task InstanceSpotMonitoringCountsDuplicateOperationOnce()
    {
        var monitoring = new ZLinkInstanceSpotMonitoring();
        var release = new TaskCompletionSource<InstanceSpotActivationTerminal>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        var first = monitoring.ObserveAsync(
            "operation-1",
            "lobby",
            128,
            () => release.Task);
        var duplicate = monitoring.ObserveAsync(
            "operation-1",
            "lobby",
            128,
            () => release.Task);

        var pending = monitoring.Snapshot("lobby");
        Assert.Equal(1UL, pending.PendingMessageCount);
        Assert.Equal(128UL, pending.PendingByteCount);

        release.SetResult(new InstanceSpotActivationTerminal(
            RequestResult.Ok,
            ServiceWireConstants.FrameworkErrorCode.None,
            Array.Empty<ReadOnlyMemory<byte>>()));
        await Task.WhenAll(first, duplicate);

        var completed = monitoring.Snapshot("lobby");
        Assert.Equal(0UL, completed.PendingMessageCount);
        Assert.Equal(0UL, completed.PendingByteCount);
        Assert.Equal("ready", completed.LastActivationOutcome);
    }

    private static ZLinkMessageFlowEvent Legacy(string packetName) =>
        new(
            ZLinkMessageFlowOutcome.Received,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Request,
            PacketName: packetName,
            ChannelName: "orders");

    private static ZLinkRuntimeMessageFlowEvent Projected(
        string packetName) =>
        new(
            "zlink.message_flow",
            DateTimeOffset.UtcNow,
            "received",
            "channel",
            "request",
            "succeeded",
            Reason: null,
            Action: null,
            MeshName: null,
            ChannelName: "orders",
            ChannelRouteKind: "client_server",
            SourceRid: null,
            TargetRid: null,
            ServerRid: null,
            packetName,
            Topic: null,
            SpotId: null,
            InstanceSpotType: null,
            ActivationState: null,
            ActorId: null,
            CorrelationId: null,
            FlowId: null,
            FlowOrigin: null,
            MessageSizeBytes: null,
            DurationSeconds: null);

    private sealed class FailOnceObserver :
        IZLinkRuntimeMessageFlowObserver
    {
        private int _first = 1;

        internal TaskCompletionSource<string> Second { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask OnMessageFlowAsync(
            ZLinkRuntimeMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (Interlocked.Exchange(ref _first, 0) != 0)
                throw new InvalidOperationException("expected");
            Second.TrySetResult(flow.PacketName!);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RecordingRuntimeErrorSink :
        Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink
    {
        internal TaskCompletionSource<ZLinkRuntimeErrorEvent> Error { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask OnRuntimeErrorAsync(
            ZLinkRuntimeErrorEvent error,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Error.TrySetResult(error);
            return ValueTask.CompletedTask;
        }
    }
}
