using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Diagnostics.HealthChecks;
using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddZLinkFramework(
        this IServiceCollection services,
        Action<IZLinkFrameworkOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        if (services.Any(static descriptor => descriptor.ServiceType == typeof(ZLinkFrameworkRegistration)))
            throw new ZLinkConfigurationException("ZLink framework is already configured.");

        var registration = new ZLinkFrameworkRegistration();
        var builder = new ZLinkFrameworkOptionsBuilder(registration);

        configure(builder);
        ZLinkFrameworkRegistrationValidator.Validate(registration);

        ZLinkFrameworkServiceRegistrar.AddFrameworkRuntime(services, registration);

        return services;
    }

    public static IServiceCollection AddZLinkMonitoring(
        this IServiceCollection services,
        Action<IZLinkMonitoringOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        if (services.Any(static descriptor => descriptor.ServiceType == typeof(ZLinkMonitoringRegistration)))
            throw new ZLinkConfigurationException("Monitoring is already configured.");

        var registration = new ZLinkMonitoringRegistration();
        var builder = new ZLinkMonitoringOptionsModel(registration);

        configure(builder);

        ZLinkMonitoringServiceRegistrar.AddMonitoringRuntime(services, registration);

        return services;
    }

    public static IServiceCollection AddZLinkHttpClient(
        this IServiceCollection services,
        string name,
        Action<ZLinkHttpClientBuilder> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(configure);

        services.TryAddSingleton<IZLinkHttpExecutionScheduler, ZLinkSpotHttpExecutionScheduler>();
        services.AddKeyedSingleton<ZLinkHttpClient>(name, (provider, _) =>
        {
            var builder = ZLinkHttpClient.Create()
                .ExecutionScheduler(provider.GetRequiredService<IZLinkHttpExecutionScheduler>());
            configure(builder);
            return builder.Build();
        });
        return services;
    }

    public static IHealthChecksBuilder AddZLinkDrainHealthCheck(
        this IHealthChecksBuilder builder)
    {
        ArgumentNullException.ThrowIfNull(builder);

        return builder.AddCheck<ZLinkDrainHealthCheck>(
            "zlink-drain",
            failureStatus: HealthStatus.Unhealthy,
            tags: ["ready"]);
    }
}
