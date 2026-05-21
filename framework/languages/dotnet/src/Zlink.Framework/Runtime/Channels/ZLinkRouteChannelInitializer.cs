using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelInitializer(
    IServiceProvider services,
    ZLinkFrameworkRegistration registration)
{
    public void Initialize(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var routedRegistration in registration.RouteChannels.Values)
        {
            var runtime = CreateRuntime(state, adapter, routedRegistration);
            foreach (var endpoint in routedRegistration.ManualConnections)
            {
                runtime.Connect(endpoint);
            }

            runtime.Start();
            state.RouteChannels.Add(routedRegistration.RouterChannelId, runtime);
        }
    }

    private ZLinkRouteChannelRuntime CreateRuntime(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        ZLinkRouteChannelRegistration routedRegistration)
    {
        var router = adapter.CreateRouterSocket(state.Context);
        router.SetChannelName(routedRegistration.RouterChannelId);
        if (routedRegistration.RoutingConfig.RoutingId.Size > 0)
        {
            router.SetRoutingId(routedRegistration.RoutingConfig.RoutingId);
        }

        router.Bind(routedRegistration.BindEndpoint!);
        var discovery = AttachDiscoveryIfNeeded(state, adapter, routedRegistration, router);
        var handlers = new ZLinkRouteHandlerRegistry(CreateRouteHandlerDescriptors(routedRegistration));
        return new ZLinkRouteChannelRuntime(
            services,
            routedRegistration,
            router,
            discovery,
            handlers,
            new ZLinkCompositeRouteInternalPacketDispatcher(
                new ZLinkSessionActorDispatchRoutePacketDispatcher(
                    services.GetRequiredService<ZLinkFrameworkRuntime>(),
                    registration),
                new ZLinkRoutedSpotRouteInternalPacketDispatcher(
                    services.GetRequiredService<ZLinkFrameworkRuntime>(),
                    registration)),
            state.StopTokenSource.Token);
    }

    private IZLinkBackendDiscovery? AttachDiscoveryIfNeeded(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        ZLinkRouteChannelRegistration routedRegistration,
        IZLinkBackendRouterSocket router)
    {
        var discoveryEndpoints = registration.Discovery?.Endpoints;
        if (routedRegistration.ManualConnections.Count > 0
            || discoveryEndpoints is null
            || discoveryEndpoints.Count == 0)
        {
            return null;
        }

        var discovery = ZLinkBackendDiscoveryFactory.Create(
            adapter,
            state.Context,
            routedRegistration.RouterChannelId,
            ZLinkAutoConnectType.RouteMesh,
            discoveryEndpoints);
        router.AttachDiscovery(discovery);
        return discovery;
    }

    private static IEnumerable<ZLinkRouteHandlerDescriptor> CreateRouteHandlerDescriptors(
        ZLinkRouteChannelRegistration routedRegistration)
    {
        foreach (var handler in routedRegistration.SendHandlers)
        {
            var method = handler.HandlerType.GetMethod(nameof(IZLinkRouteSendHandler<object>.HandleAsync))!;
            yield return new ZLinkRouteHandlerDescriptor(
                ZLinkMessageKind.Command,
                routedRegistration.RouterChannelId,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType,
                handler.MessageType,
                null,
                ZLinkHandlerMethodInvokerFactory.Create(method));
        }

        foreach (var handler in routedRegistration.RequestHandlers)
        {
            var method = handler.HandlerType.GetMethod(nameof(IZLinkRouteRequestHandler<object, object>.HandleAsync))!;
            yield return new ZLinkRouteHandlerDescriptor(
                ZLinkMessageKind.Request,
                routedRegistration.RouterChannelId,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType,
                handler.MessageType,
                handler.ReplyType,
                ZLinkHandlerMethodInvokerFactory.Create(method));
        }
    }
}
