namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringSourceValidator(
    ZLinkMonitoringRegistration registration)
{
    public void ValidateRequiredRuntimes(
        ZLinkFrameworkRuntime? frameworkRuntime,
        IZLinkLocationRuntimeQuery? locationQuery)
    {
        if (frameworkRuntime is null
            && (registration.SocketSources.Count > 0
                || registration.SpotSources.Count > 0))
            throw new ZLinkConfigurationException(
                "Monitoring socket or spot sources require AddZLinkFramework(...).");

        if (locationQuery is null && registration.HasLocationSources)
            throw new ZLinkConfigurationException(
                "Monitoring location sources require location stores registered through AddZLinkFramework(...).");
    }

    public Task PreflightPollingSourcesAsync(
        ZLinkFrameworkRuntime? frameworkRuntime,
        CancellationToken cancellationToken)
    {
        if (frameworkRuntime is null) return Task.CompletedTask;

        foreach (var source in registration.SpotSources.Values)
            try
            {
                _ = frameworkRuntime.GetSpotMonitoringSnapshot(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }

        return Task.CompletedTask;
    }
}
