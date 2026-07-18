using Zlink.Framework.ContractTests.Support;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.ContractTests.Monitoring;

public sealed class EventingContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkMessageFlowControl),
        typeof(IZLinkMessageFlowObserver),
        typeof(IZLinkMonitoringOptions),
        typeof(IZLinkRuntimeEvent),
        typeof(IZLinkRuntimeEventHandler<>),
        typeof(IZLinkRuntimeEventPublisher))]
    public async Task Eventing_contracts_wire_event_sources_to_typed_runtime_handlers()
    {
        var options = new ExampleMonitoringOptions();
        options.AddSocketEvents("router", ZLinkSocketEventKind.Connected);
        options.AddSpotEvents("spot-node", TimeSpan.FromSeconds(1));
        options.AddLocationRuntimeEvents("locations", TimeSpan.FromSeconds(1));
        options.AddLocationPeerEvents("location-peer");
        options.AddLocationSpotEvents("location-spot");
        options.AddLocationActorEvents("location-actor");
        options.AddLocationRouteEvents("location-route");

        var handler = new SocketEventHandler();
        var publisher = new ExampleRuntimeEventPublisher();
        publisher.Subscribe(handler);

        var @event = new ZLinkSocketEvent(
            "router",
            DateTimeOffset.UtcNow,
            ZLinkSocketEventKind.Connected,
            RoutingId.From("node-a"),
            "tcp://127.0.0.1:1",
            "tcp://127.0.0.1:2",
            null);

        await publisher.PublishAsync(@event, CancellationToken.None);

        Assert.Equal(
            [
                "router:socket",
                "spot-node:spot",
                "locations:location-runtime",
                "location-peer:location-peer",
                "location-spot:location-spot",
                "location-actor:location-actor",
                "location-route:location-route"
            ],
            options.Sources);
        Assert.Equal(ZLinkSocketEventKind.Connected, handler.LastEvent?.Event);
    }

    [Fact]
    [ContractExample(typeof(IZLinkDrainControl))]
    public void Observability_and_drain_contracts_match_the_frozen_surface()
    {
        Assert.Equal("zlink.framework", ZLinkMeters.Framework);
        Assert.Equal(
            new[] { "Application", "Inbound", "Lifecycle", "Timer" },
            Enum.GetNames<ZLinkFlowOrigin>().Order(StringComparer.Ordinal).ToArray());
        Assert.Equal(
            new[] { "DrainNatural", "ReleaseAndRecreate" },
            Enum.GetNames<ZLinkSpotDrainPolicy>());
        Assert.Equal(
            new[] { "DeadlineExceeded", "DrainingStatePublishFailed", "OwnerCleanupFailed", "TeardownFailed" },
            Enum.GetNames<ZLinkDrainForceReason>());
        AssertEnumValues<ZLinkMessageFlowOutcome>(
            ("Received", 0), ("Dispatched", 1), ("Replied", 2), ("Dropped", 3),
            ("Sent", 4), ("ReplyReceived", 5), ("Error", 6));
        AssertEnumValues<ZLinkDispatchErrorSurface>(
            ("Channel", 0), ("RouteMeshChannel", 1), ("SpotRoute", 2),
            ("SpotSubscription", 3), ("SpotActor", 4), ("StreamSession", 5));
        AssertEnumValues<ZLinkDispatchMessageKind>(
            ("Request", 0), ("Send", 1), ("Publish", 2), ("Response", 3),
            ("Error", 4), ("ActorRequest", 5), ("ActorSend", 6));
        AssertEnumValues<ZLinkDispatchErrorReason>(
            ("HandlerMissing", 0), ("PayloadDecodeFailed", 1), ("HandlerException", 2),
            ("InvalidFrame", 3), ("ReplyPathMissing", 4), ("UnexpectedReply", 5));
        AssertEnumValues<ZLinkDispatchErrorAction>(
            ("ReplyError", 0), ("Drop", 1), ("FailCaller", 2));
        AssertEnumValues<ZLinkDrainState>(
            ("Serving", 0), ("Draining", 1), ("Drained", 2), ("ForceStopping", 3));

        var contract = typeof(IZLinkDrainControl);
        var isReady = contract.GetProperty(nameof(IZLinkDrainControl.IsReady));
        Assert.NotNull(isReady);
        Assert.Equal(typeof(bool), isReady!.PropertyType);
        Assert.True(isReady.CanRead);
        Assert.False(isReady.CanWrite);

        AssertDrainMethod(
            contract.GetMethod(nameof(IZLinkDrainControl.DrainAsync), [typeof(CancellationToken)]),
            (typeof(CancellationToken), true));
        AssertDrainMethod(
            contract.GetMethod(
                nameof(IZLinkDrainControl.DrainAsync),
                [typeof(TimeSpan), typeof(CancellationToken)]),
            (typeof(TimeSpan), false),
            (typeof(CancellationToken), true));
        AssertDrainMethod(
            contract.GetMethod(nameof(IZLinkDrainControl.AwaitDrainedAsync), [typeof(CancellationToken)]),
            (typeof(CancellationToken), true));

        Assert.True(typeof(ZLinkDrainResult).IsAbstract);
        Assert.Empty(typeof(ZLinkDrainResult).GetConstructors());
        Assert.True(typeof(Drained).IsSealed);
        Assert.True(typeof(ForceStopped).IsSealed);
        Assert.Equal(
            typeof(ZLinkDrainForceReason),
            typeof(ForceStopped).GetProperty(nameof(ForceStopped.Reason))!.PropertyType);

        var healthExtension = typeof(ServiceCollectionExtensions).GetMethod(
            nameof(ServiceCollectionExtensions.AddZLinkDrainHealthCheck),
            [typeof(Microsoft.Extensions.DependencyInjection.IHealthChecksBuilder)]);
        Assert.NotNull(healthExtension);
        Assert.True(healthExtension!.IsPublic && healthExtension.IsStatic);
        Assert.Equal(
            typeof(Microsoft.Extensions.DependencyInjection.IHealthChecksBuilder),
            healthExtension.ReturnType);

        var flow = typeof(ZLinkMessageFlowEvent);
        Assert.Equal(typeof(string), flow.GetProperty(nameof(ZLinkMessageFlowEvent.FlowId))!.PropertyType);
        Assert.Equal(
            typeof(ZLinkFlowOrigin?),
            flow.GetProperty(nameof(ZLinkMessageFlowEvent.FlowOrigin))!.PropertyType);
        var emptyFlow = new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Received,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Send);
        Assert.Equal(string.Empty, emptyFlow.FlowId);
        Assert.Null(emptyFlow.FlowOrigin);
        var flowConstructor = Assert.Single(flow.GetConstructors());
        var flowParameters = flowConstructor.GetParameters();
        Assert.Equal(18, flowParameters.Length);
        Assert.All(flowParameters.Skip(3), static parameter => Assert.True(parameter.HasDefaultValue));
        Assert.All(flowParameters.Skip(3), static parameter => Assert.Null(parameter.DefaultValue));

        AssertClosedEventUnion(
            typeof(ZLinkLocationRuntimeEvent),
            "ServiceSummaryChanged", "StatusChanged", "StoreFailure", "StoreRecovered", "TopologyChanged");
        AssertClosedEventUnion(
            typeof(ZLinkLocationPeerEvent),
            "DesiredSetChanged", "RowRemoved", "RowUpdated");
        AssertClosedEventUnion(
            typeof(ZLinkLocationSpotEvent),
            "ResolveMiss", "RowRemoved", "RowUpdated");
        AssertClosedEventUnion(
            typeof(ZLinkLocationActorEvent),
            "ResolveMiss", "RowRemoved", "RowUpdated");
        AssertClosedEventUnion(
            typeof(ZLinkSpotEvent),
            "PeersChanged", "StatusChanged", "SubjectsChanged", "TimerHandlerFailed",
            "TimerStoppedAfterUnhandledException");

        var now = DateTimeOffset.UtcNow;
        var drainEvent = new ZLinkDrainEvent(now, ZLinkDrainState.Draining);
        Assert.Equal(now, drainEvent.Timestamp);
        Assert.Equal(ZLinkDrainState.Draining, drainEvent.State);
        Assert.Equal("drain", drainEvent.SourceName);
    }

    private static void AssertClosedEventUnion(Type root, params string[] expectedVariants)
    {
        Assert.True(root.IsAbstract);
        var constructors = root.GetConstructors(
            System.Reflection.BindingFlags.Instance
            | System.Reflection.BindingFlags.Public
            | System.Reflection.BindingFlags.NonPublic);
        Assert.DoesNotContain(constructors, static constructor => constructor.IsPublic);
        var constructor = Assert.Single(constructors.Where(candidate =>
            candidate.GetParameters() is var parameters
            && (parameters.Length != 1 || parameters[0].ParameterType != root)));
        Assert.True(constructor.IsFamilyAndAssembly);
        var variants = root
            .GetNestedTypes(System.Reflection.BindingFlags.Public)
            .OrderBy(static type => type.Name, StringComparer.Ordinal)
            .ToArray();
        Assert.Equal(expectedVariants.Order(StringComparer.Ordinal), variants.Select(static type => type.Name));
        Assert.All(variants, static variant => Assert.True(variant.IsSealed));
    }

    private static void AssertEnumValues<TEnum>(params (string Name, int Value)[] expected)
        where TEnum : struct, Enum
    {
        Assert.Equal(
            expected,
            Enum.GetValues<TEnum>().Select(static value => (value.ToString(), Convert.ToInt32(value))).ToArray());
    }

    private static void AssertDrainMethod(
        System.Reflection.MethodInfo? method,
        params (Type Type, bool HasDefault)[] expectedParameters)
    {
        Assert.NotNull(method);
        Assert.Equal(typeof(ValueTask<ZLinkDrainResult>), method!.ReturnType);
        var parameters = method.GetParameters();
        Assert.Equal(expectedParameters.Length, parameters.Length);
        for (var index = 0; index < parameters.Length; index++)
        {
            Assert.Equal(expectedParameters[index].Type, parameters[index].ParameterType);
            Assert.Equal(expectedParameters[index].HasDefault, parameters[index].HasDefaultValue);
        }
    }

    private sealed class ExampleMonitoringOptions : IZLinkMonitoringOptions
    {
        private readonly List<string> _sources = [];

        public IReadOnlyList<string> Sources => _sources;

        public void AddSocketEvents(
            string sourceName,
            params ZLinkSocketEventKind[] events)
        {
            _sources.Add($"{sourceName}:socket");
        }

        public void AddSpotEvents(
            string sourceName,
            TimeSpan interval)
        {
            _sources.Add($"{sourceName}:spot");
        }

        public void AddLocationRuntimeEvents(
            string sourceName,
            TimeSpan interval)
        {
            _sources.Add($"{sourceName}:location-runtime");
        }

        public void AddLocationPeerEvents(string sourceName)
        {
            _sources.Add($"{sourceName}:location-peer");
        }

        public void AddLocationSpotEvents(string sourceName)
        {
            _sources.Add($"{sourceName}:location-spot");
        }

        public void AddLocationActorEvents(string sourceName)
        {
            _sources.Add($"{sourceName}:location-actor");
        }

        public void AddLocationRouteEvents(string sourceName)
        {
            _sources.Add($"{sourceName}:location-route");
        }
    }

    private sealed class SocketEventHandler : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
    {
        public ZLinkSocketEvent? LastEvent { get; private set; }

        public ValueTask HandleAsync(
            ZLinkSocketEvent @event,
            CancellationToken cancellationToken)
        {
            LastEvent = @event;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ExampleRuntimeEventPublisher : IZLinkRuntimeEventPublisher
    {
        private SocketEventHandler? _handler;

        public ValueTask PublishAsync<TEvent>(
            TEvent @event,
            CancellationToken cancellationToken)
            where TEvent : IZLinkRuntimeEvent
        {
            if (@event is ZLinkSocketEvent socketEvent && _handler is not null)
                return _handler.HandleAsync(socketEvent, cancellationToken);

            return ValueTask.CompletedTask;
        }

        public void Subscribe(SocketEventHandler handler)
        {
            _handler = handler;
        }
    }
}
