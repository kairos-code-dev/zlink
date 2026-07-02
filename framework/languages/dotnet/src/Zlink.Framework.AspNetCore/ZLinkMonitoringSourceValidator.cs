namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringSourceValidator(
    ZLinkMonitoringRegistration registration)
{
    public void ValidateRequiredRuntimes(ZLinkFrameworkRuntime? frameworkRuntime)
    {
        if (frameworkRuntime is null
            && (registration.SocketSources.Count > 0
                || registration.SpotSources.Count > 0))
            throw new ZLinkConfigurationException(
                "Monitoring socket or spot sources require AddZLinkFramework(...).");
    }

    public async Task PreflightPollingSourcesAsync(
        ZLinkFrameworkRuntime? frameworkRuntime,
        CancellationToken cancellationToken)
    {
        if (frameworkRuntime is null) return;

        foreach (var source in registration.SpotSources.Values)
            try
            {
                _ = frameworkRuntime.GetSpotMonitoringSnapshot(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }

        await Task.CompletedTask;
    }
}
