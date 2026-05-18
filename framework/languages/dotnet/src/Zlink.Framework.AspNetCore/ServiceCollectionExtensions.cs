using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Zlink.Framework.Runtime.Backend.Contracts;
using System.Reflection;

namespace Zlink.Framework.AspNetCore;

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddZLinkFramework(
        this IServiceCollection services,
        Action<IZLinkFrameworkOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        var registration = new ZLinkFrameworkRegistration();
        var builder = new ZLinkFrameworkOptionsBuilder(registration);

        configure(builder);
        ZLinkFrameworkRegistrationValidator.Validate(registration);

        services
            .AddZLinkScannedHandlers(registration)
            .AddZLinkCoreRuntime(registration)
            .AddZLinkPublicClients(registration)
            .AddZLinkApplicationServices(registration);

        return services;
    }

    [Obsolete("Use AddZLinkFramework(options => options.AddHandlersFromAssemblyOf<TMarker>()) instead.")]
    public static IServiceCollection AddZLinkHandlersFromAssemblyContaining<TMarker>(
        this IServiceCollection services)
    {
        return services.AddZLinkHandlersFromAssembly(typeof(TMarker).Assembly);
    }

    public static IServiceCollection AddZLinkHandlersFromAssembly(
        this IServiceCollection services,
        Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(assembly);

        foreach (var endpoint in ZLinkHandlerScanner.Scan(assembly))
        {
            services.TryAddTransient(endpoint.DeclaringType);
            services.AddSingleton(endpoint);
        }

        return services;
    }

    public static IServiceCollection AddZLinkRegistry(
        this IServiceCollection services,
        Action<IZLinkRegistryOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        var registration = new ZLinkRegistryRegistration();
        var builder = new ZLinkRegistryOptionsModel(registration);

        configure(builder);
        ZLinkRegistryRegistrationValidator.Validate(registration);

        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.AddSingleton<ZLinkRegistryRuntime>();
        services.AddSingleton<IZLinkRegistryQuery, ZLinkRegistryQuery>();
        services.AddSingleton<Microsoft.Extensions.Hosting.IHostedService>(static provider =>
            new ZLinkRegistryHostedService(
                provider.GetRequiredService<ZLinkRegistryRuntime>(),
                provider.GetService<ZLinkFrameworkRuntime>()));

        return services;
    }

    public static IServiceCollection AddZLinkRegistryQueryClient(
        this IServiceCollection services,
        Action<IZLinkRegistryQueryClientOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        var registration = new ZLinkRegistryQueryClientRegistration();
        var builder = new ZLinkRegistryQueryClientOptionsModel(registration);

        configure(builder);
        ZLinkRegistryQueryClientRegistrationValidator.Validate(registration);

        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.AddSingleton<IZLinkRegistryQueryClient, ZLinkRegistryQueryClientService>();

        return services;
    }

    public static IServiceCollection AddZLinkMonitoring(
        this IServiceCollection services,
        Action<IZLinkMonitoringOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        if (services.Any(static descriptor => descriptor.ServiceType == typeof(ZLinkMonitoringRegistration)))
        {
            throw new ZLinkConfigurationException("Monitoring is already configured.");
        }

        var registration = new ZLinkMonitoringRegistration();
        var builder = new ZLinkMonitoringOptionsModel(registration);

        configure(builder);

        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.AddSingleton<Microsoft.Extensions.Hosting.IHostedService>(static provider =>
            new ZLinkMonitoringHostedService(
                provider,
                provider.GetRequiredService<IZLinkBackendAdapterFactory>(),
                provider.GetRequiredService<ZLinkMonitoringRegistration>(),
                provider.GetRequiredService<ZLinkRuntimeEventDispatcher>(),
                provider.GetService<ZLinkFrameworkRuntime>(),
                provider.GetService<ZLinkRegistryRuntime>()));

        return services;
    }

    private static IServiceCollection AddZLinkScannedHandlers(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var assembly in registration.HandlerAssemblies)
        {
            services.AddZLinkHandlersFromAssembly(assembly);
        }

        return services;
    }

    private static IServiceCollection AddZLinkCoreRuntime(
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

    private static IServiceCollection AddZLinkPublicClients(
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
        services.AddSingleton<IZLinkSpotManager, ZLinkSpotManagerService>();
        services.AddSingleton<IZLinkSpotClient, ZLinkSpotClientService>();
        services.AddSingleton<ZLinkSpotPublisherClientService>();
        services.AddSingleton<IZLinkSpotMeshPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        services.AddSingleton<IZLinkSpotPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        services.AddSingleton<IZLinkSpotConnectionManager, ZLinkSpotConnectionManagerService>();

        if (registration.ActorPlayRouteResolverType is not null)
        {
            services.TryAddSingleton(registration.ActorPlayRouteResolverType);
            services.AddSingleton(
                typeof(IZLinkActorPlayRouteResolver),
                provider => provider.GetRequiredService(registration.ActorPlayRouteResolverType));
        }

        if (registration.SpotRouteResolverType is not null)
        {
            services.TryAddSingleton(registration.SpotRouteResolverType);
            services.AddSingleton(
                typeof(IZLinkSpotRouteResolver),
                provider => provider.GetRequiredService(registration.SpotRouteResolverType));
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
        else
        {
            services.AddSingleton<IZLinkSessionProxyFactory, ZLinkMissingSessionProxyFactory>();
            services.AddSingleton<IZLinkActorSessionClient, ZLinkMissingSessionProxy>();
        }

        return services;
    }

    private static IServiceCollection AddZLinkApplicationServices(
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

        foreach (var streamNode in registration.StreamNodes.Values)
        {
            if (streamNode.HeaderSessionType is not null)
            {
                services.AddScoped(streamNode.HeaderSessionType);
            }
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
}
