using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkHostedService(
    ZLinkFrameworkRuntime runtime,
    ZLinkMonitoringRegistration? monitoringRegistration) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        if (monitoringRegistration is not null)
            new ZLinkMonitoringSourceValidator(monitoringRegistration).PreflightSocketSources(runtime);

        return runtime.StartAsync(cancellationToken).AsTask();
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        return runtime.StopAsync(cancellationToken).AsTask();
    }
}
