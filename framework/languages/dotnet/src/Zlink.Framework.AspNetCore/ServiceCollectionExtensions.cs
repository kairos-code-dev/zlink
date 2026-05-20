using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
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

        ZLinkFrameworkServiceRegistrar.AddFrameworkRuntime(services, registration);

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

        ZLinkMonitoringServiceRegistrar.AddMonitoringRuntime(services, registration);

        return services;
    }
}
