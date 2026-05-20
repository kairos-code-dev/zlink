using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Zlink.Framework.Runtime.Backend.Contracts;

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
            .AddApplicationServices(registration);
    }

    private static IServiceCollection AddScannedHandlers(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var assembly in registration.HandlerAssemblies)
        {
            services.AddZLinkHandlersFromAssembly(assembly);
        }

        return services;
    }

    private static IServiceCollection AddCoreRuntime(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.TryAddSingleton(static provider =>
            new ZLinkHandlerRegistry(
                provider.GetServices<ZLinkHandlerEndpointDescriptor>()));
        services.TryAddSingleton<ZLinkHandlerDispatcher>();
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.AddSingleton(static provider =>
            new ZLinkFrameworkRuntime(
                provider,
                provider.GetRequiredService<IZLinkBackendAdapterFactory>(),
                provider.GetRequiredService<ZLinkFrameworkRegistration>(),
                provider.GetRequiredService<ZLinkHandlerRegistry>(),
                provider.GetRequiredService<ZLinkHandlerDispatcher>(),
                provider.GetService<ZLinkRegistryRuntime>()));
        services.AddSingleton<IZLinkChannelConnectionManager, ZLinkChannelConnectionManager>();
        services.AddSingleton<IZLinkMessageMetadataPolicy, ZLinkMessageMetadataPolicy>();
        services.AddSingleton<Microsoft.Extensions.Hosting.IHostedService, ZLinkFrameworkHostedService>();

        return services;
    }

    private static IServiceCollection AddPublicClients(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton<ZLinkClient>();
        services.AddSingleton<IZLinkClientServerClient>(static provider => provider.GetRequiredService<ZLinkClient>());
        services.AddSingleton<IZLinkClient>(static provider => provider.GetRequiredService<ZLinkClient>());
        services.AddSingleton<ZLinkRouteClient>();
        services.AddSingleton<IZLinkRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<IZLinkMultipartRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<ZLinkEventPublisher>();
        services.AddSingleton<IZLinkFanoutPublisher>(static provider => provider.GetRequiredService<ZLinkEventPublisher>());
        services.AddSingleton<IZLinkEventPublisher>(static provider => provider.GetRequiredService<ZLinkEventPublisher>());

        if (HasSpotNode(registration))
        {
            services.AddSingleton<IZLinkSpotManager, ZLinkSpotManagerService>();
            services.AddSingleton<IZLinkSpotClient, ZLinkSpotClientService>();
            services.AddSingleton<IZLinkSpotConnectionManager, ZLinkSpotConnectionManagerService>();
        }

        if (HasSpotPublisherClient(registration))
        {
            services.AddSingleton<ZLinkSpotPublisherClientService>();
            services.AddSingleton<IZLinkSpotMeshPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
            services.AddSingleton<IZLinkSpotPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        }

        if (HasSpotNode(registration) && registration.ActorFactories.Count > 0)
        {
            services.AddSingleton<IZLinkActorManager, ZLinkActorManagerService>();
        }

        if (registration.ActorPlayRouteResolverType is not null)
        {
            services.TryAddSingleton(registration.ActorPlayRouteResolverType);
            services.AddSingleton(
                typeof(IZLinkActorPlayRouteResolver),
                provider => provider.GetRequiredService(registration.ActorPlayRouteResolverType));
        }
        else if (registration.RegistryActorRoutes is not null)
        {
            services.AddSingleton<ZLinkRegistryActorRouteResolver>();
            services.AddSingleton<IZLinkActorPlayRouteResolver>(
                static provider => provider.GetRequiredService<ZLinkRegistryActorRouteResolver>());
        }

        if (registration.SpotRouteResolverType is not null)
        {
            services.TryAddSingleton(registration.SpotRouteResolverType);
            services.AddSingleton(
                typeof(IZLinkSpotRouteResolver),
                provider => provider.GetRequiredService(registration.SpotRouteResolverType));
        }
        else if (registration.RegistrySpotRoutes is not null)
        {
            services.AddSingleton<ZLinkRegistrySpotRouteResolver>();
            services.AddSingleton<IZLinkSpotRouteResolver>(
                static provider => provider.GetRequiredService<ZLinkRegistrySpotRouteResolver>());
        }

        if (registration.ActorSessionBindingStoreType is not null)
        {
            services.TryAddSingleton(registration.ActorSessionBindingStoreType);
            services.AddSingleton(
                typeof(IZLinkActorSessionBindingStore),
                provider => provider.GetRequiredService(registration.ActorSessionBindingStoreType));
            services.AddSingleton<ZLinkSessionProxyService>();
            services.AddSingleton<IZLinkSessionProxyFactory>(
                provider => provider.GetRequiredService<ZLinkSessionProxyService>());
            services.AddSingleton<IZLinkActorSessionClient>(
                provider => provider.GetRequiredService<ZLinkSessionProxyService>());
        }
        else if (registration.RegistryActorSessionBindings is not null)
        {
            services.AddSingleton<ZLinkRegistryActorSessionBindingStore>();
            services.AddSingleton<IZLinkActorSessionBindingStore>(
                static provider => provider.GetRequiredService<ZLinkRegistryActorSessionBindingStore>());
            services.AddSingleton<ZLinkSessionProxyService>();
            services.AddSingleton<IZLinkSessionProxyFactory>(
                provider => provider.GetRequiredService<ZLinkSessionProxyService>());
            services.AddSingleton<IZLinkActorSessionClient>(
                provider => provider.GetRequiredService<ZLinkSessionProxyService>());
        }

        return services;
    }

    private static IServiceCollection AddApplicationServices(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var filterType in registration.Filters)
        {
            services.AddTransient(filterType);
        }

        foreach (var actorFactoryType in registration.ActorFactories.Values)
        {
            services.AddScoped(actorFactoryType);
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers)
            {
                services.AddScoped(handler.HandlerType);
            }

            foreach (var handler in routed.RequestHandlers)
            {
                services.AddScoped(handler.HandlerType);
            }
        }

        return services;
    }

    private static bool HasSpotNode(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Count > 0;
    }

    private static bool HasSpotPublisherClient(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Values.Any(static spotNode =>
            spotNode.AttachedSpotPublisherClients.Count > 0);
    }
}
