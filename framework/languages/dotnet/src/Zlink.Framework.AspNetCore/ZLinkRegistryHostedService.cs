using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkRegistryHostedService(
    ZLinkRegistryRuntime runtime,
    ZLinkFrameworkRuntime? frameworkRuntime) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        return runtime.StartAsync(cancellationToken).AsTask();
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        if (frameworkRuntime is not null)
        {
            return Task.CompletedTask;
        }

        return runtime.StopAsync(cancellationToken).AsTask();
    }
}
