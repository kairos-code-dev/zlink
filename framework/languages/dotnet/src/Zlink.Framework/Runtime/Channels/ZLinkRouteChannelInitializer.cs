using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

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
            state.RouteChannels.Add(routedRegistration.RouterChannelId, runtime);
            try
            {
                foreach (var endpoint in ResolveManualConnections(routedRegistration))
                {
                    runtime.Connect(endpoint);
                }

                runtime.Start();
            }
            catch
            {
                state.RouteChannels.Remove(routedRegistration.RouterChannelId);
                runtime.DisposeAsync().AsTask().GetAwaiter().GetResult();
                throw;
            }
        }
    }

    private IEnumerable<string> ResolveManualConnections(
        ZLinkRouteChannelRegistration routedRegistration)
    {
        foreach (var endpoint in routedRegistration.ManualConnections)
        {
            yield return endpoint;
        }
    }

    private ZLinkRouteChannelRuntime CreateRuntime(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        ZLinkRouteChannelRegistration routedRegistration)
    {
        IZLinkBackendRouterSocket? router = null;
        IZLinkBackendDiscovery? discovery = null;
        try
        {
            router = adapter.CreateRouterSocket(state.Context);
            router.SetChannelName(routedRegistration.RouterChannelId);
            ZLinkChannelBundleFactory.ApplySocketConfig(router, routedRegistration.SocketConfig);
            if (routedRegistration.RoutingId.Size > 0)
            {
                router.SetRoutingId(routedRegistration.RoutingId);
            }
            // weight 는 bind/discovery 前에 적용해 default-weight 노출 창을 없앤다.
            router.SetPeerWeight(routedRegistration.SocketConfig.Weight);
            router.SetMandatory(true);
            if (!string.IsNullOrWhiteSpace(routedRegistration.BindEndpoint))
            {
                router.Bind(routedRegistration.BindEndpoint);
            }
            discovery = AttachDiscoveryIfNeeded(state, adapter, routedRegistration, router);
            var handlers = new ZLinkRouteHandlerRegistry(CreateRouteHandlerDescriptors(routedRegistration));
            var runtime = new ZLinkRouteChannelRuntime(
                services,
                registration,
                routedRegistration,
                router,
                discovery,
                handlers,
                new ZLinkCompositeRouteInternalPacketDispatcher(
                    new ZLinkActorEntrySpotRouteInternalPacketDispatcher(
                        services.GetRequiredService<ZLinkFrameworkRuntime>())),
                state.StopTokenSource.Token);
            AttachSpotRouteBridgeIfAccepted(state, routedRegistration, runtime);
            return runtime;
        }
        catch
        {
            if (discovery is not null)
            {
                discovery.DisposeAsync().AsTask().GetAwaiter().GetResult();
            }

            if (router is not null)
            {
                router.DisposeAsync().AsTask().GetAwaiter().GetResult();
            }

            throw;
        }
    }

    private void AttachSpotRouteBridgeIfAccepted(
        ZLinkFrameworkRuntimeState state,
        ZLinkRouteChannelRegistration routedRegistration,
        ZLinkRouteChannelRuntime runtime)
    {
        var owner = ResolveSpotRouteBridgeOwner(routedRegistration);
        if (owner is null)
        {
            return;
        }

        if (!state.SpotNodes.TryGetValue(owner.SpotNodeName, out var spotRuntime))
        {
            throw new ZLinkConfigurationException(
                $"Route channel '{routedRegistration.RouterChannelId}' cannot attach an implicit SPOT route bridge because SPOT node '{owner.SpotNodeName}' is not started.");
        }

        var bridge = spotRuntime.Node.CreateRouteBridge();
        try
        {
            runtime.AttachSpotRouteBridge(bridge, spotRuntime);
            state.SpotRouteBridges.Add(bridge);
            state.SpotRouteBridgeOwners.Add(routedRegistration.RouterChannelId, spotRuntime);
        }
        catch
        {
            bridge.DisposeAsync().AsTask().GetAwaiter().GetResult();
            throw;
        }
    }

    private ZLinkSpotNodeRegistration? ResolveSpotRouteBridgeOwner(ZLinkRouteChannelRegistration routeChannel)
    {
        if (registration.SpotNodes.TryGetValue(routeChannel.RouterChannelId, out var named)
            && named.Router is not null)
        {
            return named;
        }

        if (routeChannel.RoutingId.Size > 0)
        {
            foreach (var spotNode in registration.SpotNodes.Values)
            {
                if (spotNode.RoutingId == routeChannel.RoutingId)
                {
                    return spotNode;
                }
            }
        }

        ZLinkSpotNodeRegistration? owner = null;
        foreach (var spotNode in registration.SpotNodes.Values)
        {
            if (spotNode.Router is null)
            {
                continue;
            }

            if (owner is not null)
            {
                return null;
            }

            owner = spotNode;
        }

        return owner;
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
        discovery.SpotOwnerSyncEnabled = true;
        router.AttachDiscovery(discovery);
        return discovery;
    }

    private IEnumerable<ZLinkRouteHandlerDescriptor> CreateRouteHandlerDescriptors(
        ZLinkRouteChannelRegistration routedRegistration)
    {
        foreach (var handler in routedRegistration.SendHandlers)
        {
            var handlerInterface = typeof(IZLinkRouteSendHandler<>).MakeGenericType(handler.MessageType);
            yield return CreateRouteHandlerDescriptor(
                routedRegistration.RouterChannelId,
                ZLinkHandlerScanner.CreateExplicitRouteInterfaceDescriptor(
                    handler.HandlerType,
                    handlerInterface,
                    ZLinkMessageKind.Command,
                    handler.PacketName));
        }

        foreach (var handler in routedRegistration.RequestHandlers)
        {
            var handlerInterface = typeof(IZLinkRouteRequestHandler<,>).MakeGenericType(
                handler.MessageType,
                handler.ReplyType!);
            yield return CreateRouteHandlerDescriptor(
                routedRegistration.RouterChannelId,
                ZLinkHandlerScanner.CreateExplicitRouteInterfaceDescriptor(
                    handler.HandlerType,
                    handlerInterface,
                    ZLinkMessageKind.Request,
                    handler.PacketName));
        }

        foreach (var assembly in registration.HandlerAssemblies)
        {
            foreach (var endpoint in ZLinkHandlerScanner.ScanRoute(assembly))
            {
                if (endpoint.Groups.Count == 0
                    || !endpoint.Groups.Any(routedRegistration.HandlerGroups.Contains))
                {
                    continue;
                }

                yield return CreateRouteHandlerDescriptor(
                    routedRegistration.RouterChannelId,
                    endpoint);
            }
        }
    }

    private static ZLinkRouteHandlerDescriptor CreateRouteHandlerDescriptor(
        string routerChannelId,
        ZLinkRouteHandlerEndpointDescriptor endpoint)
    {
        return new ZLinkRouteHandlerDescriptor(
            endpoint.Kind,
            routerChannelId,
            endpoint.MessageName,
            endpoint.DeclaringType,
            endpoint.MessageType,
            endpoint.ReplyType,
            endpoint.Invoker);
    }
}
