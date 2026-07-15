// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (Handle == IntPtr.Zero)
            return;
        Destroy(true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~SpotNode()
    {
        Destroy(false);
    }

    internal void EnsureNotDisposed()
    {
        if (Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotNode));
    }

    internal void RegisterSpot(Spot spot)
    {
        lock (_spotsGate)
        {
            _spots.Add(spot);
        }
    }

    internal void UnregisterSpot(Spot spot)
    {
        lock (_spotsGate)
        {
            _spots.Remove(spot);
        }
    }

    private void Destroy(bool throwOnError)
    {
        if (Handle == IntPtr.Zero)
            return;

        Spot[] spots;
        lock (_spotsGate)
        {
            spots = new List<Spot>(_spots).ToArray();
        }

        Exception? firstError = null;
        foreach (var spot in spots)
            try
            {
                spot.Dispose();
            }
            catch (Exception ex) when (firstError == null)
            {
                firstError = ex;
            }
            catch
            {
            }

        ActorInbox.Clear();

        var originalHandle = Handle;
        var handle = Handle;
        var rc = NativeMethods.zlink_spot_node_destroy(ref handle);
        if (rc == 0)
        {
            Handle = IntPtr.Zero;
            _sendReadyHandler = null;
            _sendReadyHandlerContext = null;
            _sendReadyHandlerNative = null;
        }
        else
        {
            Handle = originalHandle;
            if (throwOnError && firstError == null)
                firstError = ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
        }

        if (throwOnError && firstError != null)
            throw firstError;
    }

}
