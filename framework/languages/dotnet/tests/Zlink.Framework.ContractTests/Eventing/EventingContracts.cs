using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Monitoring;

public sealed class EventingContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkMonitoringOptions),
        typeof(IZLinkRuntimeEvent),
        typeof(IZLinkRuntimeEventHandler<>),
        typeof(IZLinkRuntimeEventPublisher))]
    public async Task Eventing_contracts_wire_event_sources_to_typed_runtime_handlers()
    {
        var options = new ExampleMonitoringOptions();
        options.AddSocketEvents("router", ZLinkSocketEventKind.Connected);
        options.AddRegistryEvents("registry", TimeSpan.FromSeconds(1));
        options.AddSpotEvents("spot-node", TimeSpan.FromSeconds(1));

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

        Assert.Equal(["router:socket", "registry:registry", "spot-node:spot"], options.Sources);
        Assert.Equal(ZLinkSocketEventKind.Connected, handler.LastEvent?.Event);
    }

    private sealed class ExampleMonitoringOptions : IZLinkMonitoringOptions
    {
        private readonly List<string> _sources = [];

        public IReadOnlyList<string> Sources => _sources;

        public void AddSocketEvents(
            string sourceName,
            params ZLinkSocketEventKind[] events) =>
            _sources.Add($"{sourceName}:socket");

        public void AddRegistryEvents(
            string sourceName,
            TimeSpan interval) =>
            _sources.Add($"{sourceName}:registry");

        public void AddSpotEvents(
            string sourceName,
            TimeSpan interval) =>
            _sources.Add($"{sourceName}:spot");
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

        public void Subscribe(SocketEventHandler handler) => _handler = handler;

        public ValueTask PublishAsync<TEvent>(
            TEvent @event,
            CancellationToken cancellationToken)
            where TEvent : IZLinkRuntimeEvent
        {
            if (@event is ZLinkSocketEvent socketEvent && _handler is not null)
            {
                return _handler.HandleAsync(socketEvent, cancellationToken);
            }

            return ValueTask.CompletedTask;
        }
    }
}
