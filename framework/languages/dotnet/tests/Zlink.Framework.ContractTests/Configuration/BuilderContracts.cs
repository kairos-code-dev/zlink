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
        options.ConfigureMetadata().AddForwardedMetadataKey("trace-id");
        options.AddActorFactory<ActorFactory>("player");
        options.AddSpotRemoteAddressResolver<SpotRemoteAddressResolver>();
        options.UseRegistrySpotRemoteAddresses("game").RouterChannelId = "play-router";
        options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:6000");
        options.UseFilter<HandlerFilter>();
        options.ConfigureDispatch().SpotDispatchMode = ZLinkDispatchMode.Compiled;

        Assert.Contains("trace-id", options.Metadata.ForwardedKeys);
        Assert.Contains("tcp://127.0.0.1:6000", options.Discovery.Endpoints);
        Assert.Equal("play-router", options.SpotRemoteAddresses.RouterChannelId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkClientServerChannelBuilder),
        typeof(IZLinkFanoutChannelBuilder),
        typeof(IZLinkDealerMeshChannelBuilder),
        typeof(IZLinkRouteMeshChannelBuilder))]
    public void Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel()
    {
        var options = new FrameworkOptions();

        {
            var channel = options.AddClientServerChannel("api")
                .EnableServer("tcp://127.0.0.1:5000")
                .EnableClient("tcp://127.0.0.1:5000");
            channel.AddHandlerGroup("api");
            channel.AddSendHandler<ApiSendHandler, ApiEvent>();
            channel.AddSendHandler<AttributeApiSendHandler>("api.event");
            channel.AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
            channel.AddRequestHandler<AttributeApiRequestHandler>("api.request");
            channel.EnableSpotRouteEgress("play-spots");

        }

        {
            var channel = options.AddFanoutChannel("events")
                .EnablePublisher("tcp://127.0.0.1:5100")
                .EnableSubscriber("tcp://127.0.0.1:5100");
            channel.AddHandlerGroup("events");
            channel.AddPublishHandler<EventHandler, ApiEvent>();
            channel.AddPublishHandler<AttributePublishHandler>("api.event");

        }

        {
            options.AddDealerMeshChannel("mesh")
                .EnableServer("tcp://127.0.0.1:5200")
                .EnableClient("tcp://127.0.0.1:5201");

        }

        {
            var channel = options.AddRouteMeshChannel("play-router")
                .EnableServer("tcp://127.0.0.1:5300")
                .EnableClient("tcp://127.0.0.1:5301");
            channel.AddHandlerGroup("play");
            channel.AddSendHandler<RouteSendHandler, ApiEvent>();
            channel.AddSendHandler<AttributeRouteSendHandler>("route.event");
            channel.AddRequestHandler<RouteRequestHandler, ApiRequest, ApiReply>();
            channel.AddRequestHandler<AttributeRouteRequestHandler>("route.request");
            channel.EnableSpotRouteEgress("play-spots");

        }

        Assert.Contains("api", options.Channels);
        Assert.Contains("events", options.Channels);
        Assert.Contains("mesh", options.Channels);
        Assert.Contains("play-router", options.Channels);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkStreamNodeBuilder),
        typeof(IZLinkSpotNodeBuilder),
        typeof(IZLinkSpotMeshNodeBuilder),
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
            mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:6001");
            {
                var spot = mesh.AddNode("play-spots");
                ConfigureSpotNode(spot);

            }

        }

        {
            var mesh = options.AddSpotMesh("play-mesh");
            mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:6003");
            {
                var spot = mesh.AddNode("play-spots");
                ConfigureSpotNode(spot);

            }

        }

        Assert.Contains("gateway", options.StreamNodes);
        Assert.Contains("play-spots", options.SpotNodes);
        Assert.Contains("play-mesh", options.SpotMeshes);
    }

    private static void ConfigureSpotNode(IZLinkSpotNodeBuilder spot)
    {
        spot.EnableRouter("tcp://127.0.0.1:5501")
            .SetRouterRoutingId(RoutingId.From("spot-router"))
            .ConnectRouter("tcp://127.0.0.1:5501");
        spot.ConfigureRouterSocket().TcpNoDelay = true;

        spot.EnablePubSub("tcp://127.0.0.1:5500")
            .ConnectPubSub("tcp://127.0.0.1:5502");
        spot.ConfigurePubSubPublisher().NoDrop = true;
        spot.ConfigurePubSubSubscriber().ReceiveHighWaterMark = 64;

        spot.AttachChannelClient("api", "tcp://127.0.0.1:5000");
        spot.ConfigureChannelClientSocket("api").Immediate = true;
        spot.ConfigureChannelClientRouting("api").RoutingId = RoutingId.From("spot-api-client");
        spot.AttachChannelClient("api");
        spot.AttachSpotPublisherClient("events", "tcp://127.0.0.1:5100");
        spot.AttachSpotPublisherClient("mesh-events");
        spot.AcceptSpotRoutesFromChannel("play-router", "tcp://127.0.0.1:5300");
        spot.ConfigureEntrySpot().RoutingId = RoutingId.From("entry");
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

        public IZLinkMetadataPolicyBuilder ConfigureMetadata()
        {
            return Metadata;
        }

        public void AddActorFactory<TFactory>(string actorType)
            where TFactory : class, IZLinkActorFactory { }

        public void AddSpotRemoteAddressResolver<TResolver>()
            where TResolver : class, IZLinkSpotRemoteAddressResolver { }

        public IZLinkRegistrySpotRemoteAddressesOptions UseRegistrySpotRemoteAddresses(string namespaceName) =>
            SpotRemoteAddresses;

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

        public IZLinkDealerMeshChannelBuilder AddDealerMeshChannel(string channelName)
        {
            Channels.Add(channelName);
            return new DealerMeshChannelBuilder();
        }

        public IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName)
        {
            Channels.Add(channelName);
            return new RouteMeshChannelBuilder();
        }

        public IZLinkDiscoveryBuilder UseDiscovery()
        {
            return Discovery;
        }

        public void UseFilter<TFilter>()
            where TFilter : class, IZLinkHandlerFilter { }

        public IZLinkDispatchOptions ConfigureDispatch()
        {
            return new ConnectionAndConfigContracts.DispatchOptions();
        }

        public IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName)
        {
            StreamNodes.Add(streamNodeName);
            return new StreamNodeBuilder();
        }

        public IZLinkSpotMeshBuilder AddSpotMesh(string channelName)
        {
            SpotMeshes.Add(channelName);
            return new SpotMeshBuilder(SpotNodes);
        }
    }

    private sealed class DiscoveryBuilder : IZLinkDiscoveryBuilder
    {
        public List<string> Endpoints { get; } = [];

        public IZLinkDiscoveryBuilder AddRegistryEndpoint(string endpoint)
        {
            Endpoints.Add(endpoint);
            return this;
        }
    }

    private sealed class CodecRegistryBuilder : IZLinkCodecRegistryBuilder
    {
        public void Use(IZLinkCodecExtension extension) { }

        public void AddJson() { }

        public void AddSerializer(string contentType, IZLinkMessageSerializer serializer) { }

        public void AddSerializer(
            string contentType,
            IZLinkMessageSerializer serializer,
            Func<Type, bool> canSerialize) { }

        public void AddStreamCodec(
            string contentType,
            Systems.Zlink.Stream.Connector.Contracts.ZlinkStreamCodec codec) { }
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

    private sealed class ClientServerChannelBuilder : IZLinkClientServerChannelBuilder
    {
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

        public IZLinkClientServerChannelBuilder AddHandlerGroup(string groupName) => this;

        public IZLinkClientServerChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkSendHandler<TMessage> => this;

        public IZLinkClientServerChannelBuilder AddSendHandler<THandler>(string? packetName = null)
            where THandler : class => this;

        public IZLinkClientServerChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRequestHandler<TRequest, TReply> => this;

        public IZLinkClientServerChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class => this;

        public IZLinkClientServerChannelBuilder EnableSpotRouteEgress(string targetSpotNodeChannelName) => this;
    }

    private sealed class FanoutChannelBuilder : IZLinkFanoutChannelBuilder
    {
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

        public IZLinkFanoutChannelBuilder AddHandlerGroup(string groupName) => this;

        public IZLinkFanoutChannelBuilder AddPublishHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkPublishHandler<TMessage> => this;

        public IZLinkFanoutChannelBuilder AddPublishHandler<THandler>(string? packetName = null)
            where THandler : class => this;
    }

    private sealed class DealerMeshChannelBuilder : IZLinkDealerMeshChannelBuilder
    {
        public IZLinkDealerMeshChannelBuilder EnableServer(string endpoint)
        {
            return this;
        }

        public IZLinkDealerMeshChannelBuilder EnableClient()
        {
            return this;
        }

        public IZLinkDealerMeshChannelBuilder EnableClient(string endpoint)
        {
            return this;
        }

        public IZLinkDealerMeshChannelBuilder AddHandlerGroup(string groupName) => this;

        public IZLinkDealerMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkSendHandler<TMessage> => this;

        public IZLinkDealerMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
            where THandler : class => this;

        public IZLinkDealerMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRequestHandler<TRequest, TReply> => this;

        public IZLinkDealerMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class => this;
    }

    private sealed class RouteMeshChannelBuilder : IZLinkRouteMeshChannelBuilder
    {
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
            return new ConnectionAndConfigContracts.SocketConfig();
        }

        public IZLinkRouteConfig ConfigureRouting()
        {
            return new ConnectionAndConfigContracts.RouteConfig();
        }

        public IZLinkRouteMeshChannelBuilder AddHandlerGroup(string groupName) => this;

        public IZLinkRouteMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
            where THandler : class, IZLinkRouteSendHandler<TMessage> => this;

        public IZLinkRouteMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
            where THandler : class => this;

        public IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
            where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply> => this;

        public IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
            where THandler : class => this;

        public IZLinkRouteMeshChannelBuilder EnableSpotRouteEgress(string targetSpotNodeChannelName) => this;
    }

    private sealed class StreamNodeBuilder : IZLinkStreamNodeBuilder
    {
        public IZLinkStreamNodeBuilder Bind(string endpoint) => this;

        public IZLinkStreamNodeBuilder AttachActorGateway(string spotNodeName) => this;

        public IZLinkStreamNodeBuilder RegisterSession<TSession>()
            where TSession : class, IZLinkSession => this;
    }

    private class SpotNodeBuilder : IZLinkSpotMeshNodeBuilder
    {
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

        public IZLinkSpotNodeBuilder SetRouterRoutingId(RoutingId routingId)
        {
            return this;
        }

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

        public IZLinkSpotNodeBuilder ConnectPubSub(string endpoint)
        {
            return this;
        }

        public IZLinkSpotNodeBuilder SetPubSubRoutingId(RoutingId routingId)
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

        public IZLinkSpotNodeBuilder AttachChannelClient(string channelName) => this;

        public IZLinkSpotNodeBuilder AttachChannelClient(string channelName, string endpoint) => this;

        public IZLinkSocketConfig ConfigureChannelClientSocket(string channelName)
        {
            return new ConnectionAndConfigContracts.SocketConfig();
        }

        public IZLinkOutboundRouteConfig ConfigureChannelClientRouting(string channelName)
        {
            return new ConnectionAndConfigContracts.OutboundRouteConfig();
        }

        public IZLinkSpotNodeBuilder AttachSpotPublisherClient(string channelName) => this;

        public IZLinkSpotNodeBuilder AttachSpotPublisherClient(string channelName, string endpoint) => this;

        public IZLinkSocketConfig ConfigureSpotPublisherClientSocket(string channelName)
        {
            return new ConnectionAndConfigContracts.SocketConfig();
        }

        public IZLinkSpotNodeBuilder AcceptSpotRoutesFromChannel(string channelName) => this;

        public IZLinkSpotNodeBuilder AcceptSpotRoutesFromChannel(string channelName, string endpoint) => this;

        public IZLinkEntrySpotOptions ConfigureEntrySpot()
        {
            return new ConnectionAndConfigContracts.EntrySpotOptions();
        }

        public IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
            where TSpot : IZLinkSpot => this;

        public IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
            where TEntrySpot : IZLinkEntrySpot => this;
    }

    private sealed class SpotMeshBuilder(List<string> spotNodes) : SpotNodeBuilder, IZLinkSpotMeshBuilder
    {
        public IZLinkDiscoveryBuilder UseDiscovery()
        {
            return new DiscoveryBuilder();
        }

        public IZLinkSpotMeshNodeBuilder AddNode(string spotNodeName)
        {
            spotNodes.Add(spotNodeName);
            return new SpotNodeBuilder();
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
