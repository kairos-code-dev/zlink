using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.AspNetCore;

internal static class ZLinkMonitoringServiceRegistrar
{
    public static IServiceCollection AddMonitoringRuntime(
        IServiceCollection services,
        ZLinkMonitoringRegistration registration)
    {
        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.TryAddSingleton<IZLinkRuntimeEventPublisher>(static provider =>
            provider.GetRequiredService<ZLinkRuntimeEventDispatcher>());
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
