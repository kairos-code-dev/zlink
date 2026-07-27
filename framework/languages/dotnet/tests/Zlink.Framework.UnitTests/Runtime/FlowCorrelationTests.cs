using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests;

public sealed class FlowCorrelationTests
{
    [Fact]
    public void Connector_and_framework_share_one_monotonic_correlation_sequence()
    {
        var correlations = new[]
        {
            ZlinkStreamCorrelation.Next(),
            CreateFrameworkCorrelation(),
            CreateChannelCorrelation(),
            ZlinkStreamCorrelation.Next(),
            CreateFrameworkCorrelation(),
            CreateChannelCorrelation()
        }.Select(value => Convert.ToUInt64(value, 16)).ToArray();

        for (var index = 1; index < correlations.Length; index++)
        {
            Assert.True(
                correlations[index] > correlations[index - 1],
                $"Correlation sequence did not increase: {correlations[index - 1]:x} then {correlations[index]:x}.");
        }
    }

    private static string CreateChannelCorrelation() =>
        Assert.IsType<string>(ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            "orders",
            "GetOrder").CorrelationId);

    private static string CreateFrameworkCorrelation()
    {
        var createPacketParts = typeof(ZLinkActorClient).GetMethod(
            "CreatePacketParts",
            BindingFlags.NonPublic | BindingFlags.Static);
        Assert.NotNull(createPacketParts);
        var parts = Assert.IsAssignableFrom<IReadOnlyList<Message>>(
            createPacketParts.MakeGenericMethod(typeof(string)).Invoke(
                null,
                [ZlinkStreamMessageKind.Send, null, "packet", "payload", null]));
        try
        {
            return Assert.IsType<string>(
                ZLinkStreamProtocolDefaults.DecodeHeader(parts[0].AsReadOnlyMemory()).CorrelationId);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public void UuidV7_generator_emits_the_frozen_canonical_format()
    {
        var value = ZlinkStreamFlowId.Create();

        Assert.Equal(36, value.Length);
        Assert.Equal(value.ToLowerInvariant(), value);
        Assert.Equal('7', value[14]);
        Assert.True(value[19] is '8' or '9' or 'a' or 'b');
        Assert.True(ZlinkStreamFlowId.IsValid(value));
    }

    [Fact]
    public async Task Awaited_work_keeps_flow_but_detached_work_loses_the_expired_lease()
    {
        Task<ZLinkFlowValue?> detached;
        string flowId;
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using (ZLinkFlowContext.Enter(null, null, true, ZLinkFlowOrigin.Inbound))
        {
            flowId = Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current).FlowId;
            await Task.Yield();
            Assert.Equal(flowId, ZLinkFlowContext.Current?.FlowId);

            detached = Task.Run(async () =>
            {
                await release.Task;
                return ZLinkFlowContext.Current;
            });
        }

        release.SetResult();
        Assert.Null(await detached);
        using var applicationScope = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            createIfAbsent: true);
        var application = Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current);
        Assert.NotEqual(flowId, application.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, application.Origin);
    }

    [Fact]
    public void Lifecycle_entry_and_bound_push_preserve_the_root_flow()
    {
        using var lifecycle = ZLinkFlowContext.Enter(null, null, true, ZLinkFlowOrigin.Lifecycle);
        var root = Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current);
        Assert.Equal(ZLinkFlowOrigin.Lifecycle, root.Origin);

        var createFrame = typeof(ZLinkBoundSessionService).GetMethod(
            "CreateBoundSessionFrame",
            BindingFlags.NonPublic | BindingFlags.Static);
        Assert.NotNull(createFrame);
        var frame = Assert.IsType<byte[]>(createFrame.MakeGenericMethod(typeof(string)).Invoke(
            null,
            ["push", new Dictionary<string, string>(), "payload", new ZLinkCodecRegistryBuilder()]));
        Assert.True(ZLinkStreamFrameCodec.TryDecode(frame, out var headerBytes, out _));
        var header = ZLinkStreamProtocolDefaults.DecodeHeader(headerBytes.ToArray());

        Assert.Equal(root.FlowId, header.FlowId);
        Assert.Equal((ZlinkStreamFlowOrigin)(byte)root.Origin, header.FlowOrigin);
    }

    [Fact]
    public async Task Spot_timer_callback_starts_a_timer_root_and_restores_the_caller_context()
    {
        var observed = new TaskCompletionSource<ZLinkFlowValue>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await using var registry = new ZLinkSpotTimerRegistry(static () => true);

        var timer = await registry.AddAsync(
            "flow-timer",
            TimeSpan.FromMilliseconds(5),
            new ZLinkTimerOptions { OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick },
            typeof(FlowTimerHandler),
            typeof(FlowTimerSpot),
            CancellationToken.None,
            (_, _, _) =>
            {
                observed.TrySetResult(Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current));
                return ValueTask.FromResult(true);
            },
            static (_, _, _, _, _) => ValueTask.CompletedTask,
            CancellationToken.None);

        var flow = await observed.Task.WaitAsync(TimeSpan.FromSeconds(2));
        await timer.CancelAsync();

        Assert.True(ZlinkStreamFlowId.IsValid(flow.FlowId));
        Assert.Equal(ZLinkFlowOrigin.Timer, flow.Origin);
        Assert.Null(ZLinkFlowContext.Current);
    }

    [Fact]
    public async Task Connector_outbound_flow_is_the_framework_actor_gateway_inbound_event_identity()
    {
        var sender = new ZlinkStreamFrameSender(
            new ZlinkStreamConnectorOptions
            {
                Endpoint = new Uri("tcp://127.0.0.1:1"),
                Compression = ZlinkStreamCompression.None
            },
            new ZlinkStreamHeaderCodec(),
            null,
            new SemaphoreSlim(1, 1),
            static () => null);
        var outbound = sender.BuildOutboundFrame(
            ZlinkStreamMessageKind.Send,
            "connector-to-actor",
            new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, ReadOnlyMemory<byte>.Empty),
            ZlinkStreamMetadata.Empty,
            false,
            null);
        var connectorHeader = new ZlinkStreamHeaderCodec().Decode(outbound.HeaderBytes);
        var frameworkHeader = ZLinkStreamProtocolDefaults.DecodeHeader(outbound.HeaderBytes);
        var observer = new ReceivedFlowObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkRuntimeMessageFlowMode.Off);
        options.SetRuntimeMessageFlowObserver(observer);
        await using var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var pump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var dispatcher = new ZLinkSpotActorPacketDispatcher(
            static () => null,
            static () => throw new InvalidOperationException("No handler is expected."),
            new ZLinkDispatchErrorReporter(options, observerPump: pump),
            ZLinkStandardErrorLogger.Instance);
        var actor = new FlowActor("actor-1");
        var state = new ZLinkActorRuntimeState(actor.ActorId);
        state.BindActorInstance(actor);

        using var body = Message.From(outbound.PayloadBytes.Span);
        await dispatcher.DispatchAsync(actor, state, frameworkHeader, body, CancellationToken.None);
        var received = await observer.Received.Task.WaitAsync(TimeSpan.FromSeconds(2));

        Assert.True(ZlinkStreamFlowId.IsValid(connectorHeader.FlowId));
        Assert.Equal(connectorHeader.FlowId, frameworkHeader.FlowId);
        Assert.Equal(connectorHeader.FlowId, received.FlowId);
        Assert.Equal(ZlinkStreamFlowOrigin.Application, connectorHeader.FlowOrigin);
        Assert.Equal("application", received.FlowOrigin);
        Assert.Equal(connectorHeader.CorrelationId, received.CorrelationId);
        await runner.StopAsync();
    }

    private sealed class FlowTimerSpot;

    private sealed class FlowTimerHandler : IZLinkSpotTimerHandler<FlowTimerSpot>
    {
        public ValueTask HandleAsync(
            FlowTimerSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class FlowActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = new TestActorContext(actorId);

        public void Configure()
        {
        }
    }

    private sealed class ReceivedFlowObserver : IZLinkRuntimeMessageFlowObserver
    {
        public TaskCompletionSource<ZLinkRuntimeMessageFlowEvent> Received { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask OnMessageFlowAsync(
            ZLinkRuntimeMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            if (flow.Phase == "received")
                Received.TrySetResult(flow);
            return ValueTask.CompletedTask;
        }
    }
}
