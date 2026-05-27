using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotDiscoveryLoop(
    string name,
    ZLinkRuntimeTaskRunner taskRunner,
    CancellationToken stopToken,
    Action reconcile)
{
    private static readonly TimeSpan RetryDelay = TimeSpan.FromMilliseconds(100);
    private Task? _task;

    public void StartIfNeeded(Func<bool> canRun)
    {
        if (!canRun() || _task is not null)
        {
            return;
        }

        _task = taskRunner.Run(
            $"spot-discovery-reconcile:{name}",
            _ => new ValueTask(RunAsync()));
    }

    public async ValueTask StopAsync()
    {
        if (_task is null)
        {
            return;
        }

        try
        {
            await _task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async Task RunAsync()
    {
        while (!stopToken.IsCancellationRequested)
        {
            try
            {
                reconcile();
                await Task.Delay(RetryDelay, stopToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (stopToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException) when (stopToken.IsCancellationRequested)
            {
                return;
            }
            catch (ZlinkException)
            {
                await Task.Delay(RetryDelay, stopToken).ConfigureAwait(false);
            }
        }
    }
}
