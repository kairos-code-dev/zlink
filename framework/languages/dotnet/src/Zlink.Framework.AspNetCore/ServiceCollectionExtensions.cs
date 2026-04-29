using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Zlink.Framework.Backend;
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
        services.AddSingleton<IZLinkClient, ZLinkClient>();
        services.AddSingleton<IZLinkEventPublisher, ZLinkEventPublisher>();
        services.AddSingleton<IZLinkChannelConnectionManager, ZLinkChannelConnectionManager>();
        services.AddSingleton<IZLinkSpotManager, ZLinkSpotManagerService>();
        services.AddSingleton<IZLinkSpotClient, ZLinkSpotClientService>();
        services.AddSingleton<IZLinkSpotPublisherClient, ZLinkSpotPublisherClientService>();
        services.AddSingleton<IZLinkSpotConnectionManager, ZLinkSpotConnectionManagerService>();
        services.AddSingleton<Microsoft.Extensions.Hosting.IHostedService, ZLinkFrameworkHostedService>();

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

        return services;
    }

    public static IServiceCollection AddZLinkHandlersFromAssemblyContaining<TMarker>(
        this IServiceCollection services)
    {
        ArgumentNullException.ThrowIfNull(services);

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
}
