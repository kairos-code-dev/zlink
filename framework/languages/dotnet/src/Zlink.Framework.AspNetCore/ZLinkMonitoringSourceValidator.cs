using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringSourceValidator(
    IServiceProvider services,
    ZLinkMonitoringRegistration registration)
{
    public void ValidateRequiredRuntimes(
        ZLinkFrameworkRuntime? frameworkRuntime,
        ZLinkRegistryRuntime? registryRuntime)
    {
        if (frameworkRuntime is null
            && (registration.SocketSources.Count > 0
                || registration.SpotSources.Count > 0))
        {
            throw new ZLinkConfigurationException(
                "Monitoring socket or spot sources require AddZLinkFramework(...).");
        }

        if (registryRuntime is null && registration.RegistrySources.Count > 0)
        {
            throw new ZLinkConfigurationException(
                "Monitoring registry sources require AddZLinkRegistry(...).");
        }
    }

    public async Task PreflightPollingSourcesAsync(
        ZLinkFrameworkRuntime? frameworkRuntime,
        ZLinkRegistryRuntime? registryRuntime,
        CancellationToken cancellationToken)
    {
        if (registryRuntime is not null && registration.RegistrySources.Count > 0)
        {
            _ = await services.GetRequiredService<IZLinkRegistryQuery>()
                .StatusAsync(cancellationToken);
        }

        if (frameworkRuntime is null)
        {
            return;
        }

        foreach (var source in registration.SpotSources.Values)
        {
            try
            {
                _ = frameworkRuntime.GetSpotMonitoringSnapshot(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }
        }
    }
}
