using System.Collections.Concurrent;
using System.Text;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class EntrySpotActorDispatchTests
{
    [Fact]
    public async Task EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering()
    {
        var probe = new DispatchProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<ProbeActorSendHandler>()
            .BuildServiceProvider();
        var activation = CreateActivation(services);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        Assert.True(activation.TryResolveActorPacket(
            typeof(ProbeActor),
            CreateHeader("first"),
            out var descriptor));
        Assert.NotNull(descriptor);

        var actorA = new ProbeActor("actor-a");
        var actorB = new ProbeActor("actor-b");
        var stateA = new ZLinkActorRuntimeState(actorA.ActorId);
        var stateB = new ZLinkActorRuntimeState(actorB.ActorId);

        var firstA = DispatchAsync(activation, descriptor, stateA, actorA, "first");
        await probe.ActorAFirstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var firstB = DispatchAsync(activation, descriptor, stateB, actorB, "first");
        await probe.ActorBStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var secondA = DispatchAsync(activation, descriptor, stateA, actorA, "second");
        await Task.Delay(100);
        Assert.False(probe.ActorASecondStarted.Task.IsCompleted);

        probe.ReleaseActorAFirst.SetResult();
        await probe.ActorASecondStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.WhenAll(firstA, firstB, secondA).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(
            new[]
            {
                "actor-a:first:start",
                "actor-b:first:start",
                "actor-a:first:end",
                "actor-a:second:start"
            },
            probe.Events.ToArray());
    }

    [Fact]
    public async Task EntrySpotActorDispatch_YieldCall_Throws_WhenCreatedInsideActorHandler()
    {
        var services = new ServiceCollection()
            .AddTransient<ProbeActorYieldHandler>()
            .BuildServiceProvider();
        var activation = CreateActivation(services);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        Assert.True(activation.TryResolveActorPacket(
            typeof(ProbeActor),
            CreateHeader("yield"),
            out var descriptor));
        Assert.NotNull(descriptor);

        var actor = new ProbeActor("actor-yield");
        var state = new ZLinkActorRuntimeState(actor.ActorId);
        var error = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            DispatchAsync(activation, descriptor, state, actor, "yield"));

        Assert.Contains("Yield requires a framework Spot handler turn", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task EntrySpotRouteDispatch_UsesRoutedMessagesAlreadyDrainedByBackendCallback()
    {
        var probe = new RouteDispatchProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<ProbeRouteHandler>()
            .BuildServiceProvider();
        var spot = new CapturingSpot();
        var (activation, runtime) = CreateActivationWithRuntime(services, spot);
        await using var _ = activation.ConfigureAwait(false);
        activation.Configure();

        var pump = new ZLinkEntrySpotDispatchPump(
            runtime,
            activation,
            new ZLinkRuntimeTaskRunner(new ThrowingRuntimeErrorSink(), CancellationToken.None));
        pump.Attach(spot);

        var received = CreateRoutedReceived("routed-ok");
        spot.RaiseDispatch(new ZLinkBackendSpotDispatchInfo(
            ZLinkBackendSpotDispatchEvent.RouteReadable,
            RoutedMessages: [received]));

        Assert.Equal(
            "routed-ok",
            await probe.Message.Task.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    private static async Task DispatchAsync(
        ZLinkEntrySpotActivation activation,
        ZLinkSpotActorPacketDescriptor descriptor,
        ZLinkActorRuntimeState state,
        ProbeActor actor,
        string name)
    {
        var header = CreateHeader(name);
        using var body = Message.From(Encoding.UTF8.GetBytes(name));
        await state.ExecuteDispatchAsync(
                header,
                ct => activation.InvokeActorPacketAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    ct),
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    private static ZlinkStreamHeader CreateHeader(string name)
    {
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            name,
            ZlinkStreamMetadata.Empty);
    }

    private static ZLinkEntrySpotActivation CreateActivation(IServiceProvider services)
    {
        return CreateActivationWithRuntime(services, new CapturingSpot()).Activation;
    }

    private static (ZLinkEntrySpotActivation Activation, ZLinkFrameworkRuntime Runtime) CreateActivationWithRuntime(
        IServiceProvider services,
        IZLinkBackendSpot spot)
    {
        var registration = new ZLinkFrameworkRegistration();
        var runtime = new ZLinkFrameworkRuntime(
            services,
            new ThrowingBackendAdapterFactory(),
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(services.GetRequiredService<IServiceScopeFactory>(), registration));

        return (new ZLinkEntrySpotActivation(
            runtime,
            services,
            spot,
            typeof(ProbeEntrySpot),
            RoutingId.From("entry-node"),
            "entry",
            "entry-channel",
            TimeSpan.FromSeconds(5),
            TimeSpan.FromSeconds(1)), runtime);
    }

    private static Received CreateRoutedReceived(string value)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            "entry-channel",
            nameof(ProbeRouteMessage));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            new ProbeRouteMessage(value));
        using var context = global::Systems.Zlink.Zlink.CreateContext();
        using var node = context.CreateSpotNode();
        using var sender = node.CreateSpot();
        using var receiver = node.CreateSpot();
        var nodeRid = RoutingId.From("route-source-node");
        var senderRid = RoutingId.From("route-source-spot");
        var receiverRid = RoutingId.From("route-receiver-spot");

        node.SetRoutingId(nodeRid);
        sender.SetRoutingId(senderRid);
        receiver.SetRoutingId(receiverRid);
        sender.SendToSpot(nodeRid, receiverRid)
            .Messages(parts)
            .Submit();

        var received = Received.Create();
        if (!WaitUntil(() => receiver.RecvRouted(received, RecvFlags.DontWait), TimeSpan.FromSeconds(5)))
            throw new TimeoutException("Timed out creating a routed Received message for the entry spot dispatch test.");
        return received;
    }

    private static bool WaitUntil(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow.Add(timeout);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return true;
            Thread.Sleep(10);
        }

        return false;
    }

    private sealed class DispatchProbe
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource ActorAFirstStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ActorASecondStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ActorBStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseActorAFirst { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ProbeActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => throw new NotSupportedException();
    }

    private sealed class ProbeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<ProbeRouteHandler>();
            Context.Handlers.AddActorSend<ProbeActorSendHandler, ProbeActor>("first");
            Context.Handlers.AddActorSend<ProbeActorSendHandler, ProbeActor>("second");
            Context.Handlers.AddActorSend<ProbeActorYieldHandler, ProbeActor>("yield");
        }
    }

    private sealed record ProbeRouteMessage(string Value);

    private sealed class RouteDispatchProbe
    {
        public TaskCompletionSource<string> Message { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class ProbeRouteHandler(RouteDispatchProbe probe)
        : IZLinkSpotPacketHandler<ProbeEntrySpot, ProbeRouteMessage>
    {
        public ValueTask HandleAsync(
            ProbeEntrySpot spot,
            ProbeRouteMessage message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            probe.Message.SetResult(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ProbeActorSendHandler(DispatchProbe probe)
        : IZLinkEntrySpotActorSendHandler<ProbeEntrySpot, ProbeActor, string>
    {
        public async ValueTask HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorSendContext context,
            string message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            probe.Events.Enqueue($"{actor.ActorId}:{message}:start");

            if (actor.ActorId == "actor-a" && message == "first")
            {
                probe.ActorAFirstStarted.SetResult();
                await probe.ReleaseActorAFirst.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
                probe.Events.Enqueue($"{actor.ActorId}:{message}:end");
                return;
            }

            if (actor.ActorId == "actor-a" && message == "second")
                probe.ActorASecondStarted.SetResult();

            if (actor.ActorId == "actor-b")
                probe.ActorBStarted.SetResult();
        }
    }

    private sealed class ProbeActorYieldHandler
        : IZLinkEntrySpotActorSendHandler<ProbeEntrySpot, ProbeActor, string>
    {
        public async ValueTask HandleAsync(
            ProbeEntrySpot entrySpot,
            ProbeActor actor,
            ZLinkSpotActorSendContext context,
            string request,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = context;
            await entrySpot.Context.Outbound.RequestToChannel(
                    "unused",
                    request)
                .PacketName("unused")
                .Yield<string>(cancellationToken);
        }
    }

    private sealed class CapturingSpot : IZLinkBackendSpot
    {
        private Action<ZLinkBackendSpotDispatchInfo>? _dispatchHandler;

        public object NativeInstance => this;

        public RoutingId RoutingId => RoutingId.From("entry-spot");

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId) { }

        public void SetSubscription(string topic) { }

        public bool Subscribe(TopicMessage result, RecvFlags flags) => false;

        public bool RecvRoute(Received result, RecvFlags flags) => false;

        public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
        {
            _dispatchHandler = handler;
        }

        public void RaiseDispatch(ZLinkBackendSpotDispatchInfo info)
        {
            (_dispatchHandler ?? throw new InvalidOperationException("Dispatch handler was not attached.")).Invoke(info);
        }

        public void OnSendReady(Action handler) { }

        public bool RequestToChannel(
            string channelName,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool RequestToChannel(
            string channelName,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool SendToChannel(string channelName, Message message, SendFlags flags) => false;

        public bool SendToChannel(string channelName, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool Publish(string topic, Message message, SendFlags flags) => false;

        public bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool SendToSpot(RoutingId targetRid, RoutingId spotRid, Message message, SendFlags flags) => false;

        public bool SendToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => false;

        public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags) => null;

        public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags) => null;

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            Message reply) { }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            IReadOnlyList<Message> parts) { }

        public void OnActorLifecycle(
            Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
            Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave) { }
    }

    private sealed class ThrowingBackendAdapterFactory : IZLinkBackendAdapterFactory
    {
        public IZLinkChannelBackendAdapter CreateChannelAdapter() => throw new NotSupportedException();

        public IZLinkSpotBackendAdapter CreateSpotAdapter() => throw new NotSupportedException();

        public IZLinkStreamBackendAdapter CreateStreamAdapter() => throw new NotSupportedException();

        public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => throw new NotSupportedException();
    }

    private sealed class ThrowingRuntimeErrorSink : IZLinkRuntimeErrorSink
    {
        public void ReportHandlerException(Exception exception)
        {
            throw new InvalidOperationException("Runtime handler failed.", exception);
        }

        public void ReportRuntimeTaskException(string name, Exception exception)
        {
            throw new InvalidOperationException($"Runtime task '{name}' failed.", exception);
        }
    }
}
