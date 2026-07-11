using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.AspNetCore;

internal static class ZLinkFrameworkServiceRegistrar
{
    public static IServiceCollection AddFrameworkRuntime(
        IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        return services
            .AddScannedHandlers(registration)
            .AddCoreRuntime(registration)
            .AddPublicClients(registration)
            .AddApplicationServices(registration)
            .AddLocationRuntime(registration);
    }

    private static IServiceCollection AddScannedHandlers(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var endpoint in registration.ScannedHandlerCatalog.ChannelEndpoints)
        {
            services.TryAddTransient(endpoint.DeclaringType);
            services.AddSingleton(endpoint);
        }

        foreach (var endpoint in registration.ScannedHandlerCatalog.RouteEndpoints)
            services.TryAddTransient(endpoint.DeclaringType);

        foreach (var channel in registration.Channels.Values) AddExplicitChannelHandlers(services, channel);

        return services;
    }

    private static void AddExplicitChannelHandlers(
        IServiceCollection services,
        ZLinkChannelRegistration channel)
    {
        foreach (var handler in channel.SendHandlers)
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkSendHandler<>).MakeGenericType(handler.MessageType),
                ZLinkMessageKind.Command);

        foreach (var handler in channel.RequestHandlers)
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkRequestHandler<,>).MakeGenericType(handler.MessageType, handler.ReplyType!),
                ZLinkMessageKind.Request);

        foreach (var handler in channel.PublishHandlers)
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkPublishHandler<>).MakeGenericType(handler.MessageType),
                ZLinkMessageKind.Publish);
    }

    private static void AddExplicitChannelHandler(
        IServiceCollection services,
        string channelName,
        ZLinkChannelHandlerRegistration handler,
        Type handlerInterface,
        ZLinkMessageKind kind)
    {
        services.TryAddTransient(handler.HandlerType);
        services.AddSingleton(ZLinkHandlerScanner.CreateExplicitInterfaceDescriptor(
            handler.HandlerType,
            handlerInterface,
            kind,
            channelName,
            handler.PacketName));
    }

    private static IServiceCollection AddCoreRuntime(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();

        // Install the shared, runtime-mutable message-flow mode cell (seeded from the
        // configured mode) so SetMessageFlowMode can flip tracing on/off live and
        // every surface that reads EffectiveMessageFlow observes it.
        registration.DispatchOptions.Diagnostics.LiveMode ??=
            new ZLinkMessageFlowModeCell(registration.DispatchOptions.Diagnostics.MessageFlow);

        // Public runtime toggle (resolve IZLinkMessageFlowControl to flip tracing live).
        services.AddSingleton<IZLinkMessageFlowControl>(
            new ZLinkMessageFlowControl(registration.DispatchOptions.Diagnostics));

        if (registration.DispatchOptions.MessageFlowObserverType is { } flowObserverType)
            services.TryAddTransient(flowObserverType);

        services.TryAddSingleton(static provider =>
            new ZLinkHandlerRegistry(
                provider.GetServices<ZLinkHandlerEndpointDescriptor>()));
        services.TryAddSingleton<ZLinkHandlerDispatcher>();
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.TryAddSingleton<IZLinkRuntimeEventPublisher>(static provider =>
            provider.GetRequiredService<ZLinkRuntimeEventDispatcher>());
        services.TryAddSingleton<ZLinkDrainAdmissionGate>();
        services.AddSingleton(static provider =>
            new ZLinkFrameworkRuntime(
                provider,
                provider.GetRequiredService<IZLinkBackendAdapterFactory>(),
                provider.GetRequiredService<ZLinkFrameworkRegistration>(),
                provider.GetRequiredService<ZLinkHandlerRegistry>(),
                provider.GetRequiredService<ZLinkHandlerDispatcher>()));
        services.AddSingleton<IZLinkMessageMetadataPolicy, ZLinkMessageMetadataPolicy>();
        services.TryAddSingleton<IZLinkDrainExecutor>(provider =>
            new ZLinkFrameworkDrainExecutor(
                provider.GetRequiredService<ZLinkFrameworkRuntime>(),
                registration.Locations.Options,
                provider.GetService<ZLinkLocationAutoConnectHost>(),
                provider.GetService<ZLinkLocationRuntime>(),
                provider.GetService<ILogger<ZLinkFrameworkDrainExecutor>>()));
        services.TryAddSingleton<ZLinkDrainCoordinator>(static provider =>
            new ZLinkDrainCoordinator(
                provider.GetRequiredService<ZLinkDrainAdmissionGate>(),
                provider.GetRequiredService<IZLinkDrainExecutor>(),
                provider.GetRequiredService<IZLinkRuntimeEventPublisher>(),
                () => provider.GetRequiredService<ZLinkFrameworkRegistration>()
                    .DispatchOptions.Diagnostics.EffectiveMessageFlow
                    != ZLinkMessageFlowLogMode.Off,
                provider.GetService<ILogger<ZLinkDrainCoordinator>>()));
        services.TryAddSingleton<IZLinkDrainControl>(static provider =>
            provider.GetRequiredService<ZLinkDrainCoordinator>());
        services.AddSingleton<IHostedService>(static provider =>
            new ZLinkFrameworkHostedService(
                provider.GetRequiredService<ZLinkFrameworkRuntime>(),
                provider.GetService<ZLinkMonitoringRegistration>(),
                provider.GetService<ZLinkLocationRuntime>(),
                provider.GetService<ZLinkLocationAutoConnectHost>(),
                provider.GetService<ZLinkLocationLifecycle>(),
                provider.GetRequiredService<ZLinkDrainCoordinator>()));

        return services;
    }

    private static IServiceCollection AddPublicClients(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton<ZLinkChannelClient>();
        services.AddSingleton<IZLinkChannelClient>(static provider =>
            provider.GetRequiredService<ZLinkChannelClient>());
        services.AddSingleton<IZLinkChannelRuntimeOptions>(static provider => new ZLinkChannelRuntimeOptions(
            provider.GetRequiredService<ZLinkFrameworkRuntime>()));
        services.AddSingleton<ZLinkRouteClient>();
        services.AddSingleton<IZLinkRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<ZLinkFanoutClient>();
        services.AddSingleton<IZLinkFanoutClient>(static provider => provider.GetRequiredService<ZLinkFanoutClient>());

        if (HasSpotNode(registration))
        {
            services.AddSingleton<IZLinkSpotManager>(static provider =>
                provider.GetRequiredService<ZLinkFrameworkRuntime>());
            services.AddSingleton<IZLinkSpotOutbound, ZLinkSpotOutboundService>();
            services.AddSingleton<ZLinkSpotPublisherClientService>();
            services.AddSingleton<IZLinkSpotPublisherClient>(static provider =>
                provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        }

        if (HasActorCapableSpotNode(registration))
        {
            services.AddSingleton<ZLinkActorManagerService>();
            services.AddSingleton<IZLinkActorManager>(static provider =>
                provider.GetRequiredService<ZLinkActorManagerService>());
        }

        if (HasActorCapableSpotNode(registration) || registration.Locations.Enabled)
        {
            services.AddSingleton<IZLinkActorDirectory>(static provider =>
                new ZLinkActorDirectory(
                    provider.GetRequiredService<ZLinkFrameworkRuntime>(),
                    provider.GetRequiredService<ZLinkFrameworkRegistration>(),
                    provider.GetService<ZLinkStoreLocationResolvers>()));
        }

        if (HasSpotNode(registration) && registration.Locations.Enabled)
        {
            services.AddSingleton<ZLinkActorClient>();
            services.AddSingleton<IZLinkActorClient>(static provider =>
                provider.GetRequiredService<ZLinkActorClient>());
        }

        return services;
    }

    private static IServiceCollection AddApplicationServices(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var filterType in registration.Filters) services.AddTransient(filterType);

        foreach (var actorFactoryType in registration.ActorCatalog.Factories.Values)
            services.TryAddScoped(actorFactoryType);

        foreach (var adapterType in registration.ActorCatalog.Transfers.Values
                     .Select(static transfer => transfer.AdapterType))
            services.TryAddScoped(adapterType);

        foreach (var spotType in registration.SpotNodes.Values
                     .SelectMany(static spotNode => spotNode.SpotFactories))
            services.TryAddScoped(spotType);

        foreach (var entrySpotType in registration.SpotNodes.Values
                     .Select(static spotNode => spotNode.EntrySpotType)
                     .OfType<Type>())
            services.TryAddScoped(entrySpotType);

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers) services.TryAddScoped(handler.HandlerType);

            foreach (var handler in routed.RequestHandlers) services.TryAddScoped(handler.HandlerType);
        }

        return services;
    }

    private static bool HasSpotNode(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Count > 0;
    }

    private static bool HasActorCapableSpotNode(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Values.Any(static spotNode => spotNode.ActorFactories.Count > 0);
    }
    private static IServiceCollection AddLocationRuntime(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        var locations = registration.Locations;
        if (!locations.Enabled) return services;

        services.AddSingleton(locations.Options);
        if (locations.StoreInstance is { } store)
        {
            // One physical store instance serves every store role (draft
            // 20.2); optional contracts on the same instance come along.
            services.AddSingleton(store);
            services.AddSingleton<IZLinkPeerLocationStore>(store);
            services.AddSingleton<IZLinkSpotLocationStore>(store);
            services.AddSingleton<IZLinkActorLocationStore>(store);
            services.AddSingleton<IZLinkRouteLocationStore>(store);
            services.AddSingleton<IZLinkOwnerLeaseStore>(store);
            if (store is IZLinkLocationChangeStampStore changeStamps)
                services.AddSingleton(changeStamps);
            if (store is IZLinkLocationWatchStore watch)
                services.AddSingleton(watch);
        }
        else if (locations.UseInMemoryStores)
        {
            services.AddSingleton<ZLinkInMemoryLocationStore>();
            services.AddSingleton<IZLinkLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkPeerLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkSpotLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkActorLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkRouteLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkOwnerLeaseStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkLocationChangeStampStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
        }
        else
        {
            return services;
        }

        services.AddSingleton<ZLinkLocationStoreHealth>();
        services.AddSingleton(static provider => new ZLinkOwnerLeaseTracker(
            provider.GetRequiredService<IZLinkOwnerLeaseStore>(),
            provider.GetRequiredService<ZLinkLocationOptions>(),
            health: provider.GetRequiredService<ZLinkLocationStoreHealth>()));
        // The emitter is enabled only when AddZLinkMonitoring registered
        // location sources and the dispatcher; otherwise every emit is a
        // no-op and location flows pay nothing.
        services.AddSingleton<ZLinkSpotHandleRegistry>();
        services.AddSingleton(static provider => new ZLinkLocationEventEmitter(
            provider.GetService<ZLinkMonitoringRegistration>(),
            provider.GetService<IZLinkRuntimeEventPublisher>(),
            provider.GetRequiredService<ZLinkSpotHandleRegistry>(),
            provider.GetRequiredService<ZLinkObservedLocationGenerations>()));
        // One observed-generation guard per runtime, shared by every read
        // surface, so no read path ever rolls the view backwards.
        services.AddSingleton<ZLinkObservedLocationGenerations>();
        services.AddSingleton(static provider => new ZLinkStoreLocationResolvers(
            provider.GetRequiredService<IZLinkPeerLocationStore>(),
            provider.GetRequiredService<IZLinkSpotLocationStore>(),
            provider.GetRequiredService<IZLinkActorLocationStore>(),
            provider.GetRequiredService<IZLinkRouteLocationStore>(),
            provider.GetRequiredService<ZLinkOwnerLeaseTracker>(),
            provider.GetRequiredService<ZLinkObservedLocationGenerations>(),
            events: provider.GetRequiredService<ZLinkLocationEventEmitter>(),
            health: provider.GetRequiredService<ZLinkLocationStoreHealth>()));
        services.AddSingleton<IZLinkPeerLocationResolver>(
            static provider => provider.GetRequiredService<ZLinkStoreLocationResolvers>());
        services.AddSingleton(provider => new ZLinkSpotMeshLocationResolver(
            registration,
            provider.GetRequiredService<ZLinkStoreLocationResolvers>()));
        services.AddSingleton(provider => new ZLinkLocationAddressResolvers(
            provider.GetRequiredService<ZLinkStoreLocationResolvers>(),
            provider.GetRequiredService<ZLinkSpotMeshLocationResolver>(),
            provider.GetRequiredService<ZLinkSpotHandleRegistry>()));
        services.AddSingleton<IZLinkSpotHandleResolver>(
            static provider => provider.GetRequiredService<ZLinkLocationAddressResolvers>());
        services.AddSingleton<IZLinkActorSpotHandleResolver>(
            static provider => provider.GetRequiredService<ZLinkLocationAddressResolvers>());
        services.AddSingleton(static provider => new ZLinkSpotHandleWatchHost(
            provider.GetService<IZLinkLocationWatchStore>(),
            provider.GetRequiredService<ZLinkStoreLocationResolvers>(),
            provider.GetRequiredService<ZLinkSpotHandleRegistry>(),
            provider.GetRequiredService<ZLinkLocationOptions>(),
            provider.GetRequiredService<ZLinkObservedLocationGenerations>()));
        services.AddSingleton<IHostedService>(static provider =>
            provider.GetRequiredService<ZLinkSpotHandleWatchHost>());
        services.AddSingleton(static provider => new ZLinkLocationRuntime(
            provider.GetRequiredService<ZLinkLocationOptions>(),
            provider.GetRequiredService<IZLinkLocationStore>(),
            provider.GetRequiredService<IZLinkPeerLocationStore>(),
            provider.GetRequiredService<IZLinkSpotLocationStore>(),
            provider.GetRequiredService<IZLinkActorLocationStore>(),
            provider.GetRequiredService<IZLinkRouteLocationStore>(),
            provider.GetRequiredService<IZLinkOwnerLeaseStore>(),
            events: provider.GetRequiredService<ZLinkLocationEventEmitter>()));
        services.AddSingleton<IZLinkLocationRuntimeQuery>(
            static provider => new ZLinkLocationRuntimeQueryService(
                provider.GetRequiredService<ZLinkLocationOptions>(),
                provider.GetRequiredService<IZLinkPeerLocationStore>(),
                provider.GetRequiredService<IZLinkSpotLocationStore>(),
                provider.GetRequiredService<IZLinkActorLocationStore>(),
                provider.GetRequiredService<IZLinkRouteLocationStore>(),
                provider.GetRequiredService<ZLinkOwnerLeaseTracker>(),
                provider.GetRequiredService<ZLinkLocationRuntime>(),
                provider.GetRequiredService<ZLinkObservedLocationGenerations>(),
                provider.GetService<IZLinkLocationWatchStore>() is not null,
                provider.GetRequiredService<ZLinkLocationStoreHealth>()));
        services.AddSingleton<IZLinkLocationReadiness>(
            static provider => new ZLinkLocationReadiness(
                provider.GetRequiredService<IZLinkLocationRuntimeQuery>()));
        services.AddSingleton(static provider => new ZLinkLocationLifecycle(
            provider.GetRequiredService<ZLinkLocationRuntime>(),
            provider.GetRequiredService<ZLinkStoreLocationResolvers>()));
        services.AddSingleton<IZLinkActorLocationLifecycle>(
            static provider => provider.GetRequiredService<ZLinkLocationLifecycle>().ActorOwnership);
        services.AddSingleton(static provider => new ZLinkLocationAutoConnectHost(
            provider.GetRequiredService<ZLinkLocationRuntime>(),
            provider.GetRequiredService<IZLinkPeerLocationResolver>(),
            provider.GetRequiredService<ZLinkLocationOptions>(),
            provider.GetService<IZLinkLocationChangeStampStore>(),
            provider.GetService<IZLinkLocationWatchStore>(),
            events: provider.GetRequiredService<ZLinkLocationEventEmitter>(),
            leaseTracker: provider.GetRequiredService<ZLinkOwnerLeaseTracker>()));
        services.AddSingleton<IZLinkAutoConnectTopologyQuery>(static provider =>
            provider.GetRequiredService<ZLinkLocationAutoConnectHost>());
        return services;
    }

}
