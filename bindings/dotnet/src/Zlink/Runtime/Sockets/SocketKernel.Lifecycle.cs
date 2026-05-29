// SPDX-License-Identifier: MPL-2.0

using System;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    public void Dispose()
    {
        Dispose(closeNativeSocket: !_discoveryAttached);
    }

    public void Close()
    {
        Dispose(closeNativeSocket: true);
    }

    private void Dispose(bool closeNativeSocket)
    {
        if (_streamAttached)
        {
            try
            {
                NativeMethods.zlink_stream_detach(Handle);
            }
            catch
            {
            }

            _streamAttached = false;
            _callbacks.ClearStream();
        }

        if (closeNativeSocket)
            _handle.Dispose();
        else
            _handle.ReleaseWithoutClose();
        _callbacks.ClearAllNonStream();
        GC.SuppressFinalize(this);
    }
}
