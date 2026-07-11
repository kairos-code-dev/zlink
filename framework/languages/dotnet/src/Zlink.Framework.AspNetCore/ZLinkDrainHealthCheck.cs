using Microsoft.Extensions.Diagnostics.HealthChecks;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkDrainHealthCheck(IZLinkDrainControl drain) : IHealthCheck
{
    public Task<HealthCheckResult> CheckHealthAsync(
        HealthCheckContext context,
        CancellationToken cancellationToken = default)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(
            drain.IsReady
                ? HealthCheckResult.Healthy("ZLink accepts new assignments.")
                : HealthCheckResult.Unhealthy("ZLink is draining and rejects new assignments."));
    }
}
