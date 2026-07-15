using System.Reflection;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Workers;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class BuilderContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkFrameworkOptions),
        typeof(IZLinkMetadataPolicyBuilder),
        typeof(IZLinkStreamCompressionBuilder),
        typeof(IZLinkEndpointConnections))]
    public void Framework_options_register_the_top_level_runtime_surface()
    {
        var options = new FrameworkOptions();

        options.DefaultRequestTimeout = TimeSpan.FromSeconds(5);
        options.DefaultSocketSendTimeout = TimeSpan.FromSeconds(1);
        options.AddHandlersFromAssemblyOf<BuilderContracts>();
        options.AddHandlersFromAssembly(typeof(BuilderContracts).Assembly);
        options.ConfigureMetadata().AddForwardedMetadataKey("trace-id");
        options.UseFilter<HandlerFilter>();
        options.ConfigureDispatch().MessageFlow(ZLinkMessageFlowLogMode.ErrorsOnly);

        Assert.Contains("trace-id", options.Metadata.ForwardedKeys);
        Assert.Equal(TimeSpan.FromSeconds(1), options.DefaultSocketSendTimeout);
    }

    [Fact]
    public void Nested_configuration_objects_expose_socket_and_routing_settings()
    {
        var options = new FrameworkOptions();

        options.DefaultRequestTimeout = TimeSpan.FromSeconds(3);
        Assert.Equal(TimeSpan.FromSeconds(3), options.DefaultRequestTimeout);

        var clientServer = options.AddClientServerChannel("api");
        clientServer.ConfigureClientSocket().SendTimeout = TimeSpan.FromSeconds(1);
        Assert.Equal(TimeSpan.FromSeconds(1), clientServer.ConfigureClientSocket().SendTimeout);

        var routeMesh = options.AddRouteMeshChannel("play");
        Assert.Same(
            routeMesh,
            routeMesh
                .SetRoutingId(RoutingId.From("play-a")));
        routeMesh.ConfigureSocket().SendTimeout = TimeSpan.FromSeconds(1);
        Assert.Equal(TimeSpan.FromSeconds(1), routeMesh.ConfigureSocket().SendTimeout);

        var spotMesh = options.AddSpotMesh("rooms");
        Assert.Same(spotMesh, spotMesh.SetRoutingId(RoutingId.From("rooms-a")));
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkClientServerChannelBuilder),
        typeof(IZLinkClientServerChannelOptions),
        typeof(IZLinkFanoutChannelBuilder),
        typeof(IZLinkChannelRuntimeOptions),
        typeof(IZLinkRouteMeshChannelOptions),
        typeof(IZLinkRouteMeshChannelBuilder))]
    public void Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel()
    {
        var options = new FrameworkOptions();

        {
            var channel = options.AddClientServerChannel("api")
                .EnableServer("tcp://127.0.0.1:5000")
                .EnableClient("tcp://127.0.0.1:5000")
                .SetRoutingId(RoutingId.From("api"));
            channel.ConfigureServerSocket().TcpNoDelay = true;
            channel.ConfigureClientSocket().SendTimeout = TimeSpan.FromSeconds(1);
            channel.ConfigureClientRouting().ProbeRouterOnConnect = true;
            channel.AddHandlerGroup("api");
            channel.AddSendHandler<ApiSendHandler, ApiEvent>();
            channel.AddSendHandler<AttributeApiSendHandler>("api.event");
            channel.AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
            channel.AddRequestHandler<AttributeApiRequestHandler>("api.request");
        }

        {
            var channel = options.AddFanoutChannel("events")
                .EnablePublisher("tcp://127.0.0.1:5100")
                .EnableSubscriber("tcp://127.0.0.1:5100")
                .SetRoutingId(RoutingId.From("events"));
            channel.AddHandlerGroup("events");
            channel.AddPublishHandler<EventHandler, ApiEvent>();
            channel.AddPublishHandler<AttributePublishHandler>("api.event");
        }

        {
            var channel = options.AddRouteMeshChannel("play-router")
                .EnableServer("tcp://127.0.0.1:5300")
                .EnableClient("tcp://127.0.0.1:5301")
                .SetRoutingId(RoutingId.From("play-router"));
            channel.AddHandlerGroup("play");
            channel.AddSendHandler<RouteSendHandler, ApiEvent>();
            channel.AddSendHandler<AttributeRouteSendHandler>("route.event");
            channel.AddRequestHandler<RouteRequestHandler, ApiRequest, ApiReply>();
            channel.AddRequestHandler<AttributeRouteRequestHandler>("route.request");
        }

        Assert.Contains("api", options.Channels);
        Assert.Contains("events", options.Channels);
        Assert.Contains("play-router", options.Channels);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkAllocatedRoutingIdProvider),
        typeof(IZLinkRoutingIdSlotAllocationStore))]
    public void Routing_id_allocation_is_consistent_across_every_routed_builder()
    {
        var options = new FrameworkOptions();

        var api = options.AddClientServerChannel("api");
        Assert.Same(api, api.UseAllocatedRoutingId(100, "api-").SetRoutingIdAllocationGroup("workers"));
        var events = options.AddFanoutChannel("events");
        Assert.Same(events, events.SetRoutingIdAllocationGroup("workers").UseAllocatedRoutingId(100));
        var routes = options.AddRouteMeshChannel("routes");
        Assert.Same(routes, routes.UseAllocatedRoutingId(100).SetRoutingIdAllocationGroup("workers"));
        var spots = options.AddSpotMesh("spots");
        Assert.Same(spots, spots.UseAllocatedRoutingId(100).SetRoutingIdAllocationGroup("workers"));
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkStreamNodeBuilder),
        typeof(IZLinkSpotNodeBuilder),
        typeof(IZLinkSpotMeshBuilder))]
    public void Spot_and_stream_builders_declare_node_local_roles_and_channel_attachments()
    {
        var options = new FrameworkOptions();

        {
            var stream = options.AddStreamNode("gateway");
            stream.Bind("tcp://127.0.0.1:5400");
            stream.RegisterSession<GatewaySession>();
        }

        {
            var mesh = options.AddSpotMesh("play-spots");
            Assert.Same(mesh, mesh.UseDrainPolicy(ZLinkSpotDrainPolicy.DrainNatural));
            ConfigureSpotNode(mesh);
        }

        Assert.Contains("gateway", options.StreamNodes);
        Assert.Contains("play-spots", options.SpotNodes);
        Assert.Contains("play-spots", options.SpotMeshes);
    }

    [Fact]
    public void Spot_mesh_builder_allows_multiple_process_local_nodes()
    {
        var options = new FrameworkOptions();

        options.AddSpotMesh("play-node").EnableRouter("tcp://127.0.0.1:5501");
        options.AddSpotMesh("session-node").EnableRouter("tcp://127.0.0.1:5502");

        Assert.Equal(["play-node", "session-node"], options.SpotMeshes);
        Assert.Equal(["play-node", "session-node"], options.SpotNodes);
    }

    private static void ConfigureSpotNode(IZLinkSpotNodeBuilder spot)
    {
        spot.EnableRouter("tcp://127.0.0.1:5501")
            .SetRoutingId(RoutingId.From("spot-router"))
            .ConnectRouter("tcp://127.0.0.1:5501");
        spot.ConfigureRouterSocket().TcpNoDelay = true;

        spot.EnablePubSub("tcp://127.0.0.1:5500")
            .ConnectPeerPub("tcp://127.0.0.1:5502");
        spot.ConfigurePubSubPublisher().NoDrop = true;
        spot.ConfigurePubSubSubscriber().ReceiveHighWaterMark = 64;

        spot.SetEntrySpotRoutingId(RoutingId.From("entry"))
            .AddSpotFactory<RoomSpot>()
            .AddEntrySpot<EntrySpot>()
            .AddActorFactory<ActorFactory>("player");
    }

    private sealed record ApiEvent(string Value);

    private sealed record ApiRequest(string Value);

    private sealed record ApiReply(string Value);

    private sealed class FrameworkOptions : IZLinkFrameworkOptions
    {
        public CodecRegistryBuilder Codecs { get; } = new();

        public WorkerOptions Worker { get; } = new();

        public MetadataPolicyBuilder Metadata { get; } = new();

        public List<string> Channels { get; } = [];

        public List<string> StreamNodes { get; } = [];

        public List<string> SpotNodes { get; } = [];

        public List<string> SpotMeshes { get; } = [];
        public TimeSpan DefaultRequestTimeout { get; set; }

        public TimeSpan ActorTransferForwardWindow { get; set; } = TimeSpan.FromSeconds(5);

        public TimeSpan? DefaultSocketSendTimeout { get; set; }

        IZLinkCodecRegistryBuilder IZLinkFrameworkOptions.Codecs => Codecs;

        IZLinkWorkerOptions IZLinkFrameworkOptions.Worker => Worker;

        public void AddHandlersFromAssemblyOf<TMarker>()
        {
        }

        public void AddHandlersFromAssemblyOf(Type markerType)
        {
        }

        public void AddHandlersFromAssembly(Assembly assembly)
        {
        }

        public void DisableImplicitHandlerAutoRegistration()
        {
        }

        public IZLinkMetadataPolicyBuilder ConfigureMetadata()
        {
            return Metadata;
        }

        public IZLinkClientServerChannelBuilder AddClientServerChannel(string channelName)
        {
            Channels.Add(channelName);
            return new ClientServerChannelBuilder();
        }

        public IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName)
        {
            Channels.Add(channelName);
            return new FanoutChannelBuilder();
        }

        public IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName)
        {
            Channels.Add(channelName);
            return new RouteMeshChannelBuilder();
        }

        public void UseInMemoryLocationStores()
        {
        }

        public void AddLocationStore(IZLinkLocationStore store)
        {
        }

        public ZLinkLocationOptions ConfigureLocations()
        {
            return new ZLinkLocationOptions();
        }

        public void UseFilter<TFilter>()
            where TFilter : class, IZLinkHandlerFilter
        {
        }

        public IZLinkDispatchOptions ConfigureDispatch()
        {
            return new ConnectionAndConfigContracts.DispatchOptions();
        }

        public IZLinkStreamCompressionBuilder ConfigureStreamCompression()
        {
            return new StreamCompressionBuilder();
        }

        public IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName)
        {
            StreamNodes.Add(streamNodeName);
            return new StreamNodeBuilder();
        }

        public IZLinkSpotMeshBuilder AddSpotMesh(string channelName)
        {
            SpotMeshes.Add(channelName);
            SpotNodes.Add(channelName);
            return new SpotMeshBuilder();
        }
    }

    private sealed class CodecRegistryBuilder : IZLinkCodecRegistryBuilder, IZLinkCodecRegistrar
    {
        public void Use(IZLinkCodecExtension extension)
        {
            extension.Register(this);
        }

        public void AddSerializer(string contentType, IZLinkMessageSerializer serializer)
        {
        }

        public void AddSerializer(
            string contentType,
            IZLinkMessageSerializer serializer,
            Func<Type, bool> canSerialize)
        {
        }

        public void AddStreamCodec(
            string contentType,
            ZlinkStreamCodec codec)
        {
        }
    }

    private sealed class WorkerOptions : IZLinkWorkerOptions
    {
        public int MinThreads { get; set; }

        public int MaxThreads { get; set; }

        public TimeSpan IdleTimeout { get; set; }

        public int MaxQueueLength { get; set; }
    }

    private sealed class StreamCompressionBuilder : IZLinkStreamCompressionBuilder
    {
        public IZLinkStreamCompressionBuilder UseDefault()
        {
            return this;
        }

        public IZLinkStreamCompressionBuilder UseLz4()
        {
            return this;
        }

        public IZLinkStreamCompressionBuilder Use(IZlinkStreamCompressionCodec codec)
        {
            return this;
        }

        public IZLinkStreamCompressionBuilder Disable()
        {
            return this;
        }
    }

    private sealed class MetadataPolicyBuilder : IZLinkMetadataPolicyBuilder
    {
        public List<string> ForwardedKeys { get; } = [];

        public void AddForwardedMetadataKey(string key)
        {
            ForwardedKeys.Add(key);
        }
    }

    private sealed class ClientServerChannelBuilder : IZLinkClientServerChannelBuilder
    {
        private readonly ConnectionAndConfigContracts.OutboundRouteConfig _clientRoute = new();
        private readonly ConnectionAndConfigContracts.SocketConfig _clientSocket = new();
        private readonly ConnectionAndConfigContracts.RouteConfig _serverRoute = new();
        private readonly ConnectionAndConfigContracts.SocketConfig _serverSocket = new();

        public IZLinkEndpointConnections ClientConnections { get; } = new EndpointConnections();

        public IZLinkClientServerChannelBuilder EnableServer(string endpoint)
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder EnableClient()
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder EnableClient(string endpoint)
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder SetRoutingId(RoutingId routingId)
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder UseAllocatedRoutingId(int slotCount) => this;

        public IZLinkClientServerChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix) => this;

        public IZLinkClientServerChannelBuilder SetRoutingIdAllocationGroup(string groupName) => this;

        public IZLinkSocketConfig ConfigureServerSocket()
        {
            return _serverSocket;
        }

        public IZLinkRouteConfig ConfigureServerRouting()
        {
            return _serverRoute;
        }

        public IZLinkSocketConfig ConfigureClientSocket()
        {
            return _clientSocket;
        }

        public IZLinkOutboundRouteConfig ConfigureClientRouting()
        {
            return _clientRoute;
        }

        public IZLinkClientServerChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout)
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder AddHandlerGroup(string groupName)
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkSendHandler<TMessage>
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder AddSendHandler<THandler>(string? packetName = null)
            where THandler : class
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRequestHandler<TRequest, TReply>
        {
            return this;
        }

        public IZLinkClientServerChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class
        {
            return this;
        }
    }

    private sealed class FanoutChannelBuilder : IZLinkFanoutChannelBuilder
    {
        public IZLinkEndpointConnections SubscriberConnections { get; } = new EndpointConnections();

        public IZLinkFanoutChannelBuilder EnablePublisher(string endpoint)
        {
            return this;
        }

        public IZLinkFanoutChannelBuilder EnableSubscriber()
        {
            return this;
        }

        public IZLinkFanoutChannelBuilder EnableSubscriber(string endpoint)
        {
            return this;
        }

        public IZLinkFanoutChannelBuilder SetRoutingId(RoutingId routingId)
        {
            return this;
        }

        public IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount) => this;

        public IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix) => this;

        public IZLinkFanoutChannelBuilder SetRoutingIdAllocationGroup(string groupName) => this;

        public IZLinkFanoutChannelBuilder AddHandlerGroup(string groupName)
        {
            return this;
        }

        public IZLinkFanoutChannelBuilder AddPublishHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkPublishHandler<TMessage>
        {
            return this;
        }

        public IZLinkFanoutChannelBuilder AddPublishHandler<THandler>(string? packetName = null)
            where THandler : class
        {
            return this;
        }
    }

    private sealed class RouteMeshChannelBuilder : IZLinkRouteMeshChannelBuilder
    {
        private readonly ConnectionAndConfigContracts.SocketConfig _socket = new();

        public IZLinkEndpointConnections ClientConnections { get; } = new EndpointConnections();

        public IZLinkRouteMeshChannelBuilder EnableServer(string endpoint)
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder EnableClient()
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder EnableClient(string endpoint)
        {
            return this;
        }

        public IZLinkSocketConfig ConfigureSocket()
        {
            return _socket;
        }

        public IZLinkRouteMeshChannelBuilder SetRoutingId(RoutingId routingId)
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount) => this;

        public IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix) => this;

        public IZLinkRouteMeshChannelBuilder SetRoutingIdAllocationGroup(string groupName) => this;

        public IZLinkRouteMeshChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout)
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder AddHandlerGroup(string groupName)
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkRouteSendHandler<TMessage>
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
            where THandler : class
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>
        {
            return this;
        }

        public IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class
        {
            return this;
        }
    }

    private sealed class StreamNodeBuilder : IZLinkStreamNodeBuilder
    {
        public IZLinkStreamNodeBuilder Bind(string endpoint)
        {
            return this;
        }

        public IZLinkStreamNodeBuilder SetTlsServer(string certPath, string keyPath, bool requireClientCert = false)
        {
            return this;
        }

        public IZLinkStreamNodeBuilder RegisterSession<TSession>()
            where TSession : class, IZLinkSession
        {
            return this;
        }
    }

    private class SpotNodeBuilder : IZLinkSpotNodeBuilder
    {
        public IZLinkEndpointConnections RouterConnections { get; } = new EndpointConnections();

        public IZLinkEndpointConnections PubSubConnections { get; } = new EndpointConnections();

        public IZLinkEndpointConnections ChannelClientConnections { get; } = new EndpointConnections();

        public IZLinkEndpointConnections PublisherConnections { get; } = new EndpointConnections();

        public IZLinkSpotNodeBuilder EnableRouter(string endpoint)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder ConnectRouter(string endpoint)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder SetRoutingId(RoutingId routingId)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount) => this;

        public IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix) => this;

        public IZLinkSpotNodeBuilder SetRoutingIdAllocationGroup(string groupName) => this;

        public IZLinkSocketConfig ConfigureRouterSocket()
        {
            return new ConnectionAndConfigContracts.SocketConfig();
        }

        public IZLinkRouteConfig ConfigureRouterRouting()
        {
            return new ConnectionAndConfigContracts.RouteConfig();
        }

        public IZLinkSpotNodeBuilder EnablePubSub(string endpoint)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint)
        {
            return this;
        }

        public IZLinkSpotPublisherConfig ConfigurePubSubPublisher()
        {
            return new ConnectionAndConfigContracts.SpotPublisherConfig();
        }

        public IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber()
        {
            return new ConnectionAndConfigContracts.SpotSubscriberConfig();
        }

        public IZLinkEntrySpotOptions ConfigureEntrySpot()
        {
            return new ConnectionAndConfigContracts.EntrySpotOptions();
        }

        public IZLinkSpotNodeBuilder SetEntrySpotRoutingId(RoutingId routingId)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
            where TSpot : IZLinkSpot
        {
            return this;
        }

        public IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
            where TEntrySpot : IZLinkEntrySpot
        {
            return this;
        }

        public IZLinkSpotNodeBuilder AddActorFactory<TFactory>(string actorType)
            where TFactory : class, IZLinkActorFactory
        {
            return this;
        }

        public IZLinkSpotNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
            where TActor : IZLinkActor
            where TAdapter : class, IZLinkActorTransferAdapter<TActor>
        {
            return this;
        }
    }

    private sealed class SpotMeshBuilder : SpotNodeBuilder, IZLinkSpotMeshBuilder
    {
        public IZLinkSpotMeshBuilder UseDrainPolicy(ZLinkSpotDrainPolicy policy)
        {
            return this;
        }
    }

    private sealed class ActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<IZLinkActor>(new Actor(actorId, context));
        }
    }

    private sealed class EndpointConnections : IZLinkEndpointConnections
    {
        private readonly List<string> _endpoints = [];

        public void Connect(string endpoint)
        {
            if (!_endpoints.Contains(endpoint, StringComparer.Ordinal)) _endpoints.Add(endpoint);
        }

        public void Disconnect(string endpoint) => _endpoints.Remove(endpoint);

        public IReadOnlyList<string> ListConnections() => _endpoints;
    }

    private sealed class HandlerFilter : IZLinkHandlerFilter
    {
        public ValueTask<object?> InvokeAsync(
            ZLinkHandlerInvocation invocation,
            ZLinkHandlerDelegate next,
            CancellationToken cancellationToken)
        {
            return next(cancellationToken);
        }
    }

    private sealed class Actor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }

    private sealed class GatewaySession : IZLinkSession
    {
        public IZLinkSessionContext Context => null!;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
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
        public ValueTask HandleAsync(ApiEvent message, ZLinkSendContext context, CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class AttributeApiSendHandler;

    private sealed class ApiRequestHandler : IZLinkRequestHandler<ApiRequest, ApiReply>
    {
        public ValueTask<ApiReply> HandleAsync(ApiRequest request, ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new ApiReply(request.Value));
        }
    }

    private sealed class AttributeApiRequestHandler;

    private sealed class EventHandler : IZLinkPublishHandler<ApiEvent>
    {
        public ValueTask HandleAsync(ApiEvent message, ZLinkPublishContext context, CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class AttributePublishHandler;

    private sealed class RouteSendHandler : IZLinkRouteSendHandler<ApiEvent>
    {
        public ValueTask HandleAsync(ApiEvent message, ZLinkRouteSendContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class AttributeRouteSendHandler;

    private sealed class RouteRequestHandler : IZLinkRouteRequestHandler<ApiRequest, ApiReply>
    {
        public ValueTask<ApiReply> HandleAsync(ApiRequest request, ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new ApiReply(request.Value));
        }
    }

    private sealed class AttributeRouteRequestHandler;
}
