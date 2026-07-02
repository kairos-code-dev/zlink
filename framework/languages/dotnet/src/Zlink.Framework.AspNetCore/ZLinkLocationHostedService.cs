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
    private readonly ZLinkLocationAutoConnectHost _autoConnect;
    private readonly Zlink.Framework.Runtime.Host.ZLinkFrameworkRuntime _frameworkRuntime;

    public ZLinkLocationHostedService(
        ZLinkLocationRuntime runtime,
        ZLinkLocationAutoConnectHost autoConnect,
        Zlink.Framework.Runtime.Host.ZLinkFrameworkRuntime frameworkRuntime)
    {
        _runtime = runtime;
        _autoConnect = autoConnect;
        _frameworkRuntime = frameworkRuntime;
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        // The owner id doubles as the node routing id until a configured
        // node identity is available from the channel runtimes.
        var nodeRid = RoutingId.From(_runtime.OwnerId);
        await _runtime.StartAsync(nodeRid, cancellationToken).ConfigureAwait(false);

        var state = await _frameworkRuntime.EnsureStartedStateAsync(cancellationToken)
            .ConfigureAwait(false);
        await _autoConnect.StartAsync(state, cancellationToken).ConfigureAwait(false);
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        // Stop reconcile loops (which remove the local peer rows) before
        // the runtime drops the owner lease and bulk-removes leftovers.
        await _autoConnect.StopAsync(cancellationToken).ConfigureAwait(false);
        await _runtime.StopAsync(cancellationToken).ConfigureAwait(false);
    }
}
