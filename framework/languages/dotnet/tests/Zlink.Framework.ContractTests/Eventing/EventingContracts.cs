using Zlink.Framework.ContractTests.Support;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.ContractTests.Monitoring;

public sealed class EventingContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkMessageFlowRuntime),
        typeof(IZLinkRuntimeMessageFlowObserver),
        typeof(IZLinkMonitoringOptions),
        typeof(IZLinkRuntimeEvent),
        typeof(IZLinkRuntimeEventHandler<>))]
    public async Task Eventing_contracts_wire_event_sources_to_typed_runtime_handlers()
    {
        var options = new ExampleMonitoringOptions();
        options.AddSocketEvents("router", ZLinkSocketEventKind.Connected);
        options.AddMeshNodeEvents("orders");
        options.AddLocationRuntimeEvents("locations", TimeSpan.FromSeconds(1));

        var handler = new SocketEventHandler();
        var @event = new ZLinkSocketEvent(
            "router",
            DateTimeOffset.UtcNow,
            ZLinkSocketEventKind.Connected,
            RoutingId.From("node-a"),
            "tcp://127.0.0.1:1",
            "tcp://127.0.0.1:2");

        await handler.HandleAsync(@event, CancellationToken.None);

        Assert.Equal(
            [
                "router:socket",
                "orders:mesh",
                "locations:location-runtime"
            ],
            options.Sources);
        Assert.Equal(ZLinkSocketEventKind.Connected, handler.LastEvent?.Event);
    }

    [Fact]
    [ContractExample(typeof(IZLinkFrameworkRuntime))]
    public void Observability_and_termination_contracts_match_the_exact_surface()
    {
        var frameworkAssembly = typeof(IZLinkMonitoringOptions).Assembly;
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.IZLinkRuntimeEventPublisher"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.ZLinkSocketNativeEventType"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.ZLinkDrainState"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.ZLinkDrainEvent"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.ZLinkLocationPeerEvent"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.ZLinkLocationSpotEvent"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Eventing.ZLinkLocationActorEvent"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Dispatch.IZLinkMessageFlowControl"));
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Dispatch.IZLinkMessageFlowObserver"));
        Assert.False(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Dispatch.ZLinkMessageFlowEvent")!.IsPublic);
        Assert.Null(frameworkAssembly.GetType(
            "Zlink.Framework.Contracts.Dispatch.ZLinkMessageFlowLogMode"));
        Assert.Null(typeof(ZLinkSocketEvent).GetProperty("Diagnostic"));
        Assert.Equal(6, Assert.Single(typeof(ZLinkSocketEvent).GetConstructors()).GetParameters().Length);

        Assert.Equal("zlink.framework", ZLinkMeters.Framework);
        Assert.Equal(
            new[] { "Application", "Inbound", "Lifecycle", "Timer" },
            Enum.GetNames<ZLinkFlowOrigin>().Order(StringComparer.Ordinal).ToArray());
        AssertEnumValues<ZLinkRuntimeMessageFlowMode>(
            ("Off", 0), ("ErrorsOnly", 1), ("KeyTransitions", 2), ("Verbose", 3));
        var contract = typeof(IZLinkFrameworkRuntime);
        var isReady = contract.GetProperty(nameof(IZLinkFrameworkRuntime.IsReady));
        Assert.NotNull(isReady);
        Assert.Equal(typeof(bool), isReady!.PropertyType);
        Assert.True(isReady.CanRead);
        Assert.False(isReady.CanWrite);

        Assert.NotNull(contract.GetMethod(nameof(IZLinkFrameworkRuntime.RetireAsync)));
        Assert.NotNull(contract.GetMethod(nameof(IZLinkFrameworkRuntime.ShutdownAsync)));
        Assert.Null(contract.GetMethod("DrainAsync"));
        Assert.Null(contract.GetMethod("AwaitDrainedAsync"));

        var healthExtension = typeof(ServiceCollectionExtensions).GetMethod(
            nameof(ServiceCollectionExtensions.AddZLinkDrainHealthCheck),
            [typeof(Microsoft.Extensions.DependencyInjection.IHealthChecksBuilder)]);
        Assert.NotNull(healthExtension);
        Assert.True(healthExtension!.IsPublic && healthExtension.IsStatic);
        Assert.Equal(
            typeof(Microsoft.Extensions.DependencyInjection.IHealthChecksBuilder),
            healthExtension.ReturnType);

        var flow = typeof(ZLinkRuntimeMessageFlowEvent);
        Assert.Equal(typeof(string), flow.GetProperty(nameof(ZLinkRuntimeMessageFlowEvent.FlowId))!.PropertyType);
        Assert.Equal(
            typeof(string),
            flow.GetProperty(nameof(ZLinkRuntimeMessageFlowEvent.FlowOrigin))!.PropertyType);
        var flowConstructor = Assert.Single(flow.GetConstructors());
        var flowParameters = flowConstructor.GetParameters();
        Assert.Equal(25, flowParameters.Length);
        Assert.All(flowParameters, static parameter => Assert.False(parameter.HasDefaultValue));

        AssertClosedEventUnion(
            typeof(ZLinkLocationRuntimeEvent),
            "ServiceSummaryChanged", "StatusChanged", "StoreFailure", "StoreRecovered", "TopologyChanged");
        AssertClosedEventUnion(
            typeof(ZLinkSpotEvent),
            "TimerHandlerFailed", "TimerStoppedAfterUnhandledException");

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

        public void AddMeshNodeEvents(string meshName)
        {
            _sources.Add($"{meshName}:mesh");
        }

        public void AddLocationRuntimeEvents(
            string sourceName,
            TimeSpan interval)
        {
            _sources.Add($"{sourceName}:location-runtime");
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

}
