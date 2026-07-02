// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    public void Dispose()
    {
        Dispose(true);
    }

    public void Close()
    {
        Dispose(true);
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
