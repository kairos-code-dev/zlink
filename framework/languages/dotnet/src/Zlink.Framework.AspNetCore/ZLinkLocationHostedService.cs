using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.AspNetCore;

/// <summary>
/// Starts the location runtime with the host: registers the owner lease
/// before anything else advertises rows, keeps the heartbeat running, and
/// on shutdown removes the lease and bulk-removes this owner's rows.
/// </summary>
internal sealed class ZLinkLocationHostedService : IHostedService
{
    private readonly ZLinkLocationRuntime _runtime;

    public ZLinkLocationHostedService(ZLinkLocationRuntime runtime)
    {
        _runtime = runtime;
    }

    public Task StartAsync(CancellationToken cancellationToken)
    {
        // The owner id doubles as the node routing id until the auto
        // connect runtime supplies the configured node identity.
        var nodeRid = RoutingId.From(_runtime.OwnerId);
        return _runtime.StartAsync(nodeRid, cancellationToken).AsTask();
    }

    public Task StopAsync(CancellationToken cancellationToken) =>
        _runtime.StopAsync(cancellationToken).AsTask();
}
