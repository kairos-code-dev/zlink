// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~SpotNode()
    {
        Destroy(throwOnError: false);
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotNode));
    }

    internal bool TryGetChannelDealerHandle(string channelName, out IntPtr handle)
    {
        lock (_channelDealers)
        {
            if (_channelDealers.TryGetValue(channelName, out DealerSocket? dealer))
            {
                handle = dealer.Handle;
                return handle != IntPtr.Zero;
            }
        }

        handle = IntPtr.Zero;
        return false;
    }

    internal void RegisterSpot(Spot spot)
    {
        lock (_spotsGate)
            _spots.Add(spot);
    }

    internal void UnregisterSpot(Spot spot)
    {
        lock (_spotsGate)
            _spots.Remove(spot);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        Spot[] spots;
        lock (_spotsGate)
            spots = new List<Spot>(_spots).ToArray();

        Exception? firstError = null;
        foreach (Spot spot in spots)
        {
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
        }

        lock (_channelDealers)
            _channelDealers.Clear();
        _sendReadyHandler = null;
        _sendReadyHandlerContext = null;
        _sendReadyHandlerNative = null;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_spot_node_destroy(ref handle);
        if (rc == 0)
        {
            _handle = IntPtr.Zero;
        }
        else
        {
            _handle = originalHandle;
            if (throwOnError && firstError == null)
                firstError = ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
        }

        if (throwOnError && firstError != null)
            throw firstError;
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _sendReadyHandler;
        SynchronizationContext? context = _sendReadyHandlerContext;
        if (handler == null)
            return;
        CallbackDelivery.Post(context, () =>
        {
            try
            {
                handler();
            }
            catch (Exception ex)
            {
                CallbackExceptionHub.Report(ex);
            }
        });
    }
}
