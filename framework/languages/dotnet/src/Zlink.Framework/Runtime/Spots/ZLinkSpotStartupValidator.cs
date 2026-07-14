using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotStartupValidator
{
    public static async ValueTask ValidateAsync(
        IServiceProvider services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var spotNode in registration.SpotNodes.Values)
        foreach (var spotType in spotNode.SpotFactories)
        {
            await using var scope = services.CreateAsyncScope();
            var context = new PacketRegistrationContext();
            var spot = (IZLinkSpot)ActivatorUtilities.CreateInstance(
                scope.ServiceProvider,
                spotType,
                context);
            if (!ReferenceEquals(spot.Context, context))
                throw new ZLinkConfigurationException(
                    $"SPOT '{spotType}' must expose the context provided by the runtime.");

            foreach (var handler in registration.ScannedHandlerCatalog.SpotHandlers)
                context.AddScannedPacket(spotType, handler);

            spot.Configure();
            context.Validate(spot);
        }
    }

    private sealed class PacketRegistrationContext :
        IZLinkSpotContext,
        IZLinkSpotHandlerRegistrySink
    {
        private readonly ZLinkSpotPacketRegistry _packets = new();

        public PacketRegistrationContext()
        {
            Handlers = new ZLinkSpotHandlerRegistrySurface(this);
        }

        public RoutingId SpotRid => default;

        public RoutingId NodeRid => default;

        public IZLinkSpotHandlerRegistry Handlers { get; }

        public IZLinkSpotOutbound Outbound => throw ConfigurationOnly();

        public ValueTask<IZLinkTimer> AddTimer<THandler>(
            string name,
            TimeSpan period,
            ZLinkTimerOptions? options = null,
            CancellationToken cancellationToken = default)
            where THandler : class => throw ConfigurationOnly();

        public IZLinkWorkerCall<TResult> RunWorker<TResult>(
            Func<CancellationToken, TResult> work) => throw ConfigurationOnly();

        public ValueTask LeaveActorAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default) => throw ConfigurationOnly();

        public ValueTask<bool> CloseAsync(
            CancellationToken cancellationToken = default) => throw ConfigurationOnly();

        public void AddPacket<THandler>() where THandler : class =>
            _packets.Add(typeof(THandler));

        public void AddSubscribe<THandler>(string topic) where THandler : class
        {
        }

        public void AddHandler<THandler>() where THandler : class
        {
        }

        public void AddHandler<THandler>(string packetName) where THandler : class
        {
        }

        public void AddActorPacket<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor
        {
        }

        public void AddActorPacket<THandler, TActor>(string packetName)
            where THandler : class
            where TActor : IZLinkActor
        {
        }

        public void AddScannedPacket(
            Type spotType,
            ZLinkScannedSpotHandler handler)
        {
            if (handler.Kind != ZLinkScannedSpotHandlerKind.Packet
                || handler.SpotType != spotType) return;

            _packets.Add(handler);
        }

        public void Validate(IZLinkSpot spot) => _packets.Bind(spot);

        private static ZLinkConfigurationException ConfigurationOnly() => new(
            "SPOT lifecycle operations are not available while startup configuration is being validated.");
    }
}
