using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Workflow.Support;

internal sealed class StaleSpotHandleProbe
{
    private readonly object _gate = new();
    private SpotHandle? _handle;

    public void Capture(SpotHandle handle)
    {
        lock (_gate) _handle = handle;
    }

    public async ValueTask<StaleHandleProbeRes> ExecuteAsync(
        IZLinkSpotClient routes,
        CancellationToken cancellationToken)
    {
        SpotHandle handle;
        lock (_gate)
            handle = _handle
                     ?? throw new InvalidOperationException("A Spot handle has not been captured.");
        try
        {
            _ = await routes.RequestToSpot(handle, new ReadWorkflowReq())
                .Async<ReadWorkflowRes>(cancellationToken);
            return new StaleHandleProbeRes(false, null);
        }
        catch (Exception error)
        {
            return new StaleHandleProbeRes(true, error.GetType().Name);
        }
    }
}
