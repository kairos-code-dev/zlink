using Zlink.Framework.ContractTests.Support;
using Zlink.Framework.Contracts.Workers;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class BuilderContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkFrameworkOptions),
        typeof(IZLinkDiscoveryBuilder),
        typeof(IZLinkMetadataPolicyBuilder),
        typeof(IZLinkRegistrySpotRemoteAddressesOptions))]
    public void Framework_options_register_the_top_level_runtime_surface()
    {
        var options = new FrameworkOptions();

        options.DefaultTimeout = TimeSpan.FromSeconds(5);
        options.Codecs.AddJson();
        options.AddHandlersFromAssemblyOf<BuilderContracts>();
        options.AddHandlersFromAssembly(typeof(BuilderContracts).Assembly);
        options.ConfigureMetadata(metadata => metadata.AddForwardedMetadataKey("trace-id"));
        options.AddActorFactory<ActorFactory>("player");
        options.AddSpotRemoteAddressResolver<SpotRemoteAddressResolver>();
        options.UseRegistrySpotRemoteAddresses("game", registry => registry.RouterChannelId = "play-router");
        options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:6000"));
        options.UseFilter<HandlerFilter>();
        options.ConfigureDispatch(dispatch => dispatch.SpotDispatchMode = ZLinkDispatchMode.Compiled);

        Assert.Contains("trace-id", options.Metadata.ForwardedKeys);
        Assert.Contains("tcp://127.0.0.1:6000", options.Discovery.Endpoints);
        Assert.Equal("play-router", options.SpotRemoteAddresses.RouterChannelId);
    }

    [Fact]
    [ContractExample(
        typeof(IChannelServerCapabilityBuilder),
        typeof(IChannelClientCapabilityBuilder),
        typeof(IDealerMeshChannelClientCapabilityBuilder),
        typeof(IChannelPublisherCapabilityBuilder),
        typeof(IChannelSubscriberCapabilityBuilder),
        typeof(IZLinkClientServerChannelBuilder),
        typeof(IZLinkFanoutChannelBuilder),
        typeof(IZLinkDealerMeshChannelBuilder),
        typeof(IZLinkRouteChannelBuilder),
        typeof(IZLinkRouteMeshChannelBuilder),
        typeof(IZLinkEndpointConnections))]
    public void Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel()
    {
        var options = new FrameworkOptions();

        options.AddClientServerChannel("api", channel =>
        {
            channel.EnableServer(server =>
            {
                server.Bind("tcp://127.0.0.1:5000");
                server.ConfigureSocket(socket => socket.TcpNoDelay = true);
                server.ConfigureRouting(route => route.RoutingId = RoutingId.From("api-server"));
            });
            channel.EnableClient(client =>
            {
                client.ConfigureSocket(socket => socket.Immediate = true);
                client.ConfigureRouting(route => route.ProbeRouterOnConnect = true);
                client.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5000"));
            });
            channel.AddHandlerGroup("api");
            channel.AddSendHandler<ApiSendHandler, ApiEvent>();
            channel.AddSendHandler<AttributeApiSendHandler>("api.event");
            channel.AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
            channel.AddRequestHandler<AttributeApiRequestHandler>("api.request");
            channel.EnableSpotRouteEgress("play-spots");
        });

        options.AddFanoutChannel("events", channel =>
        {
            channel.EnablePublisher(publisher =>
            {
                publisher.Bind("tcp://127.0.0.1:5100");
                publisher.ConfigureSocket(socket => socket.SendHighWaterMark = 100);
            });
            channel.EnableSubscriber(subscriber =>
            {
                subscriber.ConfigureSocket(socket => socket.ReceiveHighWaterMark = 100);
                subscriber.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5100"));
            });
            channel.AddHandlerGroup("events");
            channel.AddPublishHandler<EventHandler, ApiEvent>();
            channel.AddPublishHandler<AttributePublishHandler>("api.event");
        });

        options.AddDealerMeshChannel("mesh", channel =>
        {
            channel.EnableClient(client =>
            {
                client.Bind("tcp://127.0.0.1:5200");
                client.ConfigureSocket(socket => socket.TcpNoDelay = true);
                client.ConfigureRouting(route => route.RoutingId = RoutingId.From("dealer"));
                client.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5201"));
            });
        });

        options.AddRouteMeshChannel("play-router", channel =>
        {
            channel.Bind("tcp://127.0.0.1:5300");
            channel.ConfigureSocket(socket => socket.TcpNoDelay = true);
            channel.ConfigureRouting(route => route.RoutingId = RoutingId.From("router"));
            channel.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5301"));
            channel.AddHandlerGroup("play");
            channel.AddSendHandler<RouteSendHandler, ApiEvent>();
            channel.AddSendHandler<AttributeRouteSendHandler>("route.event");
            channel.AddRequestHandler<RouteRequestHandler, ApiRequest, ApiReply>();
            channel.AddRequestHandler<AttributeRouteRequestHandler>("route.request");
            channel.EnableSpotRouteEgress("play-spots");
        });

        Assert.Contains("api", options.Channels);
        Assert.Contains("events", options.Channels);
        Assert.Contains("mesh", options.Channels);
        Assert.Contains("play-router", options.Channels);
    }

    [Fact]
    [ContractExample(
        typeof(ISpotRouterCapabilityBuilder),
        typeof(ISpotPubSubCapabilityBuilder),
        typeof(ISpotPublisherClientCapabilityBuilder),
        typeof(ISpotChannelClientCapabilityBuilder),
        typeof(IZLinkStreamNodeBuilder),
        typeof(IZLinkSpotNodeBuilder),
        typeof(IZLinkSpotMeshNodeBuilder),
        typeof(IZLinkSpotRouteChannelAcceptanceBuilder),
        typeof(IZLinkEndpointConnections),
        typeof(IZLinkSpotMeshBuilder))]
    public void Spot_and_stream_builders_declare_node_local_roles_and_channel_attachments()
    {
        var options = new FrameworkOptions();

        options.AddStreamNode("gateway", stream =>
        {
            stream.Bind("tcp://127.0.0.1:5400");
            stream.RegisterSession<GatewaySession>();
        });

        options.AddSpotMesh("play-spots", mesh =>
        {
            mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:6001"));
            mesh.AddNode("play-spots", spot =>
            {
                ConfigureSpotNode(spot);
            });
        });

        options.AddSpotMesh("play-mesh", mesh =>
        {
            mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:6003"));
            mesh.AddNode("play-spots", spot =>
            {
                ConfigureSpotNode(spot);
            });
        });

        Assert.Contains("gateway", options.StreamNodes);
        Assert.Contains("play-spots", options.SpotNodes);
        Assert.Contains("play-mesh", options.SpotMeshes);
    }

    private static void ConfigureSpotNode(IZLinkSpotNodeBuilder spot)
    {
        spot.EnableRouter(router =>
        {
            router.BindRouter("tcp://127.0.0.1:5501");
            router.ConfigureSocket(socket => socket.TcpNoDelay = true);
            router.SetRoutingId(RoutingId.From("spot-router"));
            router.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5501"));
        });
        spot.EnablePubSub(pubSub =>
        {
            pubSub.BindPubSub("tcp://127.0.0.1:5500");
            pubSub.ConfigurePublisher(publisher => publisher.NoDrop = true);
            pubSub.ConfigureSubscriber(subscriber => subscriber.ReceiveHighWaterMark = 64);
            pubSub.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5502"));
        });
        spot.AttachChannelClient("api", client =>
        {
            client.ConfigureSocket(socket => socket.Immediate = true);
            client.ConfigureRouting(route => route.RoutingId = RoutingId.From("spot-api-client"));
            client.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5000"));
        });
        spot.AttachChannelClient("api");
        spot.AttachSpotPublisherClient("events", client => client.UseManualConnections(
            connections => connections.Connect("tcp://127.0.0.1:5100")));
        spot.AttachSpotPublisherClient("mesh-events");
        spot.AcceptSpotRoutesFromChannel("play-router", accept =>
            accept.UseManualConnections(connections => connections.Connect("tcp://127.0.0.1:5300")));
        spot.ConfigureEntrySpot(entry => entry.RoutingId = RoutingId.From("entry"));
        spot.AddSpotFactory<RoomSpot>();
        spot.AddEntrySpot<EntrySpot>();
    }

    private sealed record ApiEvent(string Value);

    private sealed record ApiRequest(string Value);

    private sealed record ApiReply(string Value);

    private sealed class FrameworkOptions : IZLinkFrameworkOptions
    {
        public TimeSpan DefaultTimeout { get; set; }

        public CodecRegistryBuilder Codecs { get; } = new();

        IZLinkCodecRegistryBuilder IZLinkFrameworkOptions.Codecs => Codecs;

        public WorkerOptions Worker { get; } = new();

        IZLinkWorkerOptions IZLinkFrameworkOptions.Worker => Worker;

        public MetadataPolicyBuilder Metadata { get; } = new();

        public DiscoveryBuilder Discovery { get; } = new();

        public RegistrySpotRemoteAddressesOptions SpotRemoteAddresses { get; } = new();

        public List<string> Channels { get; } = [];

        public List<string> StreamNodes { get; } = [];

        public List<string> SpotNodes { get; } = [];

        public List<string> SpotMeshes { get; } = [];

        public void AddHandlersFromAssemblyOf<TMarker>() { }

        public void AddHandlersFromAssemblyOf(Type markerType) { }

        public void AddHandlersFromAssembly(System.Reflection.Assembly assembly) { }

        public void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure) => configure(Metadata);

        public void AddActorFactory<TFactory>(string actorType)
            where TFactory : class, IZLinkActorFactory { }

        public void AddSpotRemoteAddressResolver<TResolver>()
            where TResolver : class, IZLinkSpotRemoteAddressResolver { }

        public void UseRegistrySpotRemoteAddresses(string namespaceName) { }

        public void UseRegistrySpotRemoteAddresses(
            string namespaceName,
            Action<IZLinkRegistrySpotRemoteAddressesOptions> configure) =>
            configure(SpotRemoteAddresses);

        public void AddClientServerChannel(
            string channelName,
            Action<IZLinkClientServerChannelBuilder> configure)
        {
            Channels.Add(channelName);
            configure(new ClientServerChannelBuilder());
        }

        public void AddFanoutChannel(
            string channelName,
            Action<IZLinkFanoutChannelBuilder> configure)
        {
            Channels.Add(channelName);
            configure(new FanoutChannelBuilder());
        }

        public void AddDealerMeshChannel(
            string channelName,
            Action<IZLinkDealerMeshChannelBuilder> configure)
        {
            Channels.Add(channelName);
            configure(new DealerMeshChannelBuilder());
        }

        public void AddRouteMeshChannel(
            string channelName,
            Action<IZLinkRouteMeshChannelBuilder> configure)
        {
            Channels.Add(channelName);
            configure(new RouteMeshChannelBuilder());
        }

        public void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure) => configure(Discovery);

        public void UseFilter<TFilter>()
            where TFilter : class, IZLinkHandlerFilter { }

        public void ConfigureDispatch(Action<IZLinkDispatchOptions> configure) =>
            configure(new ConnectionAndConfigContracts.DispatchOptions());

        public void AddStreamNode(
            string streamNodeName,
            Action<IZLinkStreamNodeBuilder> configure)
        {
            StreamNodes.Add(streamNodeName);
            configure(new StreamNodeBuilder());
        }

        public void AddSpotMesh(
            string channelName,
            Action<IZLinkSpotMeshBuilder> configure)
        {
            SpotMeshes.Add(channelName);
            configure(new SpotMeshBuilder(SpotNodes));
        }
    }

    private sealed class DiscoveryBuilder : IZLinkDiscoveryBuilder
    {
        public List<string> Endpoints { get; } = [];

        public void AddRegistryEndpoint(string endpoint) => Endpoints.Add(endpoint);
    }

    private sealed class CodecRegistryBuilder : IZLinkCodecRegistryBuilder
    {
        public void AddProtobuf() { }

        public void AddJson() { }

        public void AddMessagePack() { }
    }

    private sealed class WorkerOptions : IZLinkWorkerOptions
    {
        public int MinThreads { get; set; }

        public int MaxThreads { get; set; }

        public TimeSpan IdleTimeout { get; set; }

        public int MaxQueueLength { get; set; }
    }

    private sealed class MetadataPolicyBuilder : IZLinkMetadataPolicyBuilder
    {
        public List<string> ForwardedKeys { get; } = [];

        public void AddForwardedMetadataKey(string key) => ForwardedKeys.Add(key);
    }

    private sealed class RegistrySpotRemoteAddressesOptions : IZLinkRegistrySpotRemoteAddressesOptions
    {
        public string? RouterChannelId { get; set; }
    }

    private sealed class CapabilityBuilder :
        IChannelServerCapabilityBuilder,
        IChannelClientCapabilityBuilder,
        IDealerMeshChannelClientCapabilityBuilder,
        IChannelPublisherCapabilityBuilder,
        IChannelSubscriberCapabilityBuilder,
        ISpotRouterCapabilityBuilder,
        ISpotPubSubCapabilityBuilder,
        ISpotPublisherClientCapabilityBuilder,
        ISpotChannelClientCapabilityBuilder
    {
        public void Bind(string endpoint) { }

        public void BindRouter(string endpoint) { }

        public void BindPubSub(string endpoint) { }

        public void SetRoutingId(RoutingId routingId) { }

        public void ConfigureSocket(Action<IZLinkSocketConfig> configure) =>
            configure(new ConnectionAndConfigContracts.SocketConfig());

        public void ConfigureRouting(Action<IZLinkRouteConfig> configure) =>
            configure(new ConnectionAndConfigContracts.RouteConfig());

        public void ConfigureRouting(Action<IZLinkOutboundRouteConfig> configure) =>
            configure(new ConnectionAndConfigContracts.OutboundRouteConfig());

        public void UseManualConnections(Action<IZLinkEndpointConnections> configure) =>
            configure(new ConnectionAndConfigContracts.ManualConnections());

        public void ConfigurePublisher(Action<IZLinkSpotPublisherConfig> configure) =>
            configure(new ConnectionAndConfigContracts.SpotPublisherConfig());

        public void ConfigureSubscriber(Action<IZLinkSpotSubscriberConfig> configure) =>
            configure(new ConnectionAndConfigContracts.SpotSubscriberConfig());
    }

    private sealed class ClientServerChannelBuilder : IZLinkClientServerChannelBuilder
    {
        public void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void AddHandlerGroup(string groupName) { }

        public void AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkSendHandler<TMessage> { }

        public void AddSendHandler<THandler>(string? packetName = null)
            where THandler : class { }

        public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRequestHandler<TRequest, TReply> { }

        public void AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class { }

        public void EnableSpotRouteEgress(string targetSpotNodeChannelName) { }
    }

    private sealed class FanoutChannelBuilder : IZLinkFanoutChannelBuilder
    {
        public void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void AddHandlerGroup(string groupName) { }

        public void AddPublishHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkPublishHandler<TMessage> { }

        public void AddPublishHandler<THandler>(string? packetName = null)
            where THandler : class { }
    }

    private sealed class DealerMeshChannelBuilder : IZLinkDealerMeshChannelBuilder
    {
        public void EnableClient(Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void AddHandlerGroup(string groupName) { }

        public void AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkSendHandler<TMessage> { }

        public void AddSendHandler<THandler>(string? packetName = null)
            where THandler : class { }

        public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRequestHandler<TRequest, TReply> { }

        public void AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class { }
    }

    private sealed class RouteMeshChannelBuilder : IZLinkRouteMeshChannelBuilder
    {
        public void Bind(string endpoint) { }

        public void ConfigureSocket(Action<IZLinkSocketConfig> configure) =>
            configure(new ConnectionAndConfigContracts.SocketConfig());

        public void ConfigureRouting(Action<IZLinkRouteConfig> configure) =>
            configure(new ConnectionAndConfigContracts.RouteConfig());

        public void UseManualConnections(Action<IZLinkEndpointConnections> configure) =>
            configure(new ConnectionAndConfigContracts.ManualConnections());

        public void AddHandlerGroup(string groupName) { }

        public void AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkRouteSendHandler<TMessage> { }

        public void AddSendHandler<THandler>(string? packetName = null)
            where THandler : class { }

        public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply> { }

        public void AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class { }

        public void EnableSpotRouteEgress(string targetSpotNodeChannelName) { }
    }

    private sealed class StreamNodeBuilder : IZLinkStreamNodeBuilder
    {
        public void Bind(string endpoint) { }

        public void AttachActorGateway(string spotNodeName) { }

        public void RegisterSession<TSession>()
            where TSession : class, IZLinkSession { }
    }

    private sealed class SpotNodeBuilder : IZLinkSpotMeshNodeBuilder
    {
        public void BindRouter(string endpoint) { }

        public void BindPubSub(string endpoint) { }

        public void EnableRouter(Action<ISpotRouterCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void EnablePubSub(Action<ISpotPubSubCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void AttachChannelClient(
            string channelName,
            Action<ISpotChannelClientCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void AttachSpotPublisherClient(
            string channelName,
            Action<ISpotPublisherClientCapabilityBuilder>? configure = null) =>
            configure?.Invoke(new CapabilityBuilder());

        public void AcceptSpotRoutesFromChannel(
            string channelName,
            Action<IZLinkSpotRouteChannelAcceptanceBuilder>? configure = null) =>
            configure?.Invoke(new SpotRouteChannelAcceptanceBuilder());

        public void ConfigureEntrySpot(Action<IZLinkEntrySpotOptions> configure) =>
            configure(new ConnectionAndConfigContracts.EntrySpotOptions());

        public void AddSpotFactory<TSpot>()
            where TSpot : IZLinkSpot { }

        public void AddEntrySpot<TEntrySpot>()
            where TEntrySpot : IZLinkEntrySpot { }
    }

    private sealed class SpotRouteChannelAcceptanceBuilder : IZLinkSpotRouteChannelAcceptanceBuilder
    {
        public void UseManualConnections(Action<IZLinkEndpointConnections> configure) =>
            configure(new ConnectionAndConfigContracts.ManualConnections());
    }

    private sealed class SpotMeshBuilder(List<string> spotNodes) : IZLinkSpotMeshBuilder
    {
        public void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure) => configure(new DiscoveryBuilder());

        public void AddNode(
            string spotNodeName,
            Action<IZLinkSpotMeshNodeBuilder> configure)
        {
            spotNodes.Add(spotNodeName);
            configure(new SpotNodeBuilder());
        }
    }

    private sealed class ActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkActor>(new Actor(actorId, context));
    }

    private sealed class SpotRemoteAddressResolver : IZLinkSpotRemoteAddressResolver
    {
        public ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ZLinkSpotRemoteAddress(
                "play-router",
                RoutingId.From("node"),
                spotRid,
                ZLinkSpotKind.User));
    }

    private sealed class HandlerFilter : IZLinkHandlerFilter
    {
        public ValueTask<object?> InvokeAsync(
            ZLinkHandlerInvocation invocation,
            ZLinkHandlerDelegate next,
            CancellationToken cancellationToken) =>
            next(cancellationToken);
    }

    private sealed class Actor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }

    private sealed class GatewaySession : IZLinkSession
    {
        public IZLinkSessionContext Context => null!;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class RoomSpot : IZLinkSpot
    {
        public IZLinkSpotContext Context => null!;
    }

    private sealed class EntrySpot : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context => null!;
    }

    private sealed class ApiSendHandler : IZLinkSendHandler<ApiEvent>
    {
        public ValueTask HandleAsync(ApiEvent message, ZLinkSendContext context, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class AttributeApiSendHandler;

    private sealed class ApiRequestHandler : IZLinkRequestHandler<ApiRequest, ApiReply>
    {
        public ValueTask<ApiReply> HandleAsync(ApiRequest request, ZLinkRequestContext context, CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ApiReply(request.Value));
    }

    private sealed class AttributeApiRequestHandler;

    private sealed class EventHandler : IZLinkPublishHandler<ApiEvent>
    {
        public ValueTask HandleAsync(ApiEvent message, ZLinkPublishContext context, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class AttributePublishHandler;

    private sealed class RouteSendHandler : IZLinkRouteSendHandler<ApiEvent>
    {
        public ValueTask HandleAsync(ApiEvent message, ZLinkRouteSendContext context, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class AttributeRouteSendHandler;

    private sealed class RouteRequestHandler : IZLinkRouteRequestHandler<ApiRequest, ApiReply>
    {
        public ValueTask<ApiReply> HandleAsync(ApiRequest request, ZLinkRouteRequestContext context, CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ApiReply(request.Value));
    }

    private sealed class AttributeRouteRequestHandler;
}
