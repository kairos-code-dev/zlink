using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkHostedService(ZLinkFrameworkRuntime runtime) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        return runtime.StartAsync(cancellationToken).AsTask();
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        return runtime.StopAsync(cancellationToken).AsTask();
    }
}
