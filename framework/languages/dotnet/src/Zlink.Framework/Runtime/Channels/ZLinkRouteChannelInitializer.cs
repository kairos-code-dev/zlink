using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelInitializer(
    IServiceProvider services,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask InitializeAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var routedRegistration in registration.RouteChannels.Values)
        {
            var runtime = await CreateRuntimeAsync(state, adapter, routedRegistration).ConfigureAwait(false);
            state.RouteChannels.Add(routedRegistration.RouterChannelId, runtime);
            try
            {
                if (routedRegistration.AcquisitionMode == ZLinkPeerAcquisitionMode.Manual)
                    routedRegistration.ManualConnections.Attach(runtime.ConnectManual, runtime.DisconnectManual);

                runtime.Start();
            }
            catch (Exception initializationFailure)
            {
                state.RouteChannels.Remove(routedRegistration.RouterChannelId);
                var failures = new ZLinkFailureCollector(initializationFailure);
                await failures.CaptureAsync(runtime.DisposeAsync).ConfigureAwait(false);
                failures.ThrowIfAny();
                throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
            }
        }
    }

    private async ValueTask<ZLinkRouteChannelRuntime> CreateRuntimeAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        ZLinkRouteChannelRegistration routedRegistration)
    {
        IZLinkBackendRouterSocket? router = null;
        ZLinkRouteChannelRuntime? runtime = null;
        try
        {
            router = adapter.CreateRouterSocket(state.Context);
            router.SetChannelName(routedRegistration.RouterChannelId);
            ZLinkChannelBundleFactory.ApplySocketConfig(router, routedRegistration.SocketConfig);
            if (routedRegistration.RoutingId.Size > 0) router.SetRoutingId(routedRegistration.RoutingId);
            // weight 는 bind 前에 적용해 default-weight 노출 창을 없앤다.
            router.SetPeerWeight(routedRegistration.SocketConfig.Weight);
            router.SetMandatory(true);
            router.SetHandover(true);
            if (!string.IsNullOrWhiteSpace(routedRegistration.BindEndpoint))
                router.Bind(routedRegistration.BindEndpoint);
            var handlers = new ZLinkRouteHandlerRegistry(CreateRouteHandlerDescriptors(routedRegistration));
            runtime = new ZLinkRouteChannelRuntime(
                services,
                registration,
                routedRegistration,
                router,
                handlers,
                new ZLinkCompositeRouteInternalPacketDispatcher(
                    new ZLinkActorEntrySpotRouteInternalPacketDispatcher(
                        services.GetRequiredService<ZLinkFrameworkRuntime>())),
                state.StopTokenSource.Token,
                state.TaskRunner.ExecutionOwner,
                services.GetRequiredService<ZLinkFrameworkRuntime>());
            return runtime;
        }
        catch (Exception initializationFailure)
        {
            var failures = new ZLinkFailureCollector(initializationFailure);
            if (runtime is not null)
                await failures.CaptureAsync(runtime.DisposeAsync).ConfigureAwait(false);
            else if (router is not null)
                await failures.CaptureAsync(router.DisposeAsync).ConfigureAwait(false);
            failures.ThrowIfAny();
            throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
        }
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

        foreach (var endpoint in registration.ScannedHandlerCatalog.RouteEndpoints)
        {
            if (endpoint.Groups.Count == 0
                || !endpoint.Groups.Any(routedRegistration.HandlerGroups.Contains))
                continue;

            yield return CreateRouteHandlerDescriptor(
                routedRegistration.RouterChannelId,
                endpoint);
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
