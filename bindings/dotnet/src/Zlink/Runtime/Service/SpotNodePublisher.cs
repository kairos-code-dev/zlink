// SPDX-License-Identifier: MPL-2.0

using System.Buffers;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class SpotNodePublisher : ISpotNodePublisher
{
    private const int StackPartLimit = 8;
    private IntPtr _handle;

    internal SpotNodePublisher(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        node.EnsureNotDisposed();
        _handle = NativeMethods.zlink_spot_node_publisher_new(node.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public bool Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        BoundaryValidation.ValidateFixedUtf8(topic, nameof(topic));
        EnsureNotDisposed();
        try
        {
            SubmitParts(parts, (nativeParts, partCount) =>
            {
                return NativeMethods.zlink_spot_node_publisher_publish(
                    _handle, topic, nativeParts, partCount, (int)flags);
            });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
        }
    }

    public void Close()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    public void Dispose()
    {
        Close();
    }

    public ValueTask DisposeAsync()
    {
        Close();
        return ValueTask.CompletedTask;
    }

    ~SpotNodePublisher()
    {
        Dispose(false);
    }

    private static unsafe void SubmitParts(IReadOnlyList<Message> parts,
        NativePublisherSubmitter submit)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        var cloned = RequestReplySupport.CloneParts(parts);
        ZlinkMsg[]? rented = null;
        var nativeParts = cloned.Length <= StackPartLimit
            ? stackalloc ZlinkMsg[StackPartLimit]
            : rented = ArrayPool<ZlinkMsg>.Shared.Rent(cloned.Length);
        nativeParts = nativeParts.Slice(0, cloned.Length);
        var built = 0;
        try
        {
            NativeMessageParts.MoveToNative(cloned, nativeParts, nameof(parts),
                ref built);
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                var rc = submit((IntPtr)nativePtr, (nuint)built);
                ZlinkException.ThrowSubmitIfError(rc);
            }
        }
        finally
        {
            for (var i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
            RequestReplySupport.DisposeParts(cloned);
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotNodePublisher));
    }

    private void Dispose(bool disposing)
    {
        _ = disposing;
        var handle = _handle;
        if (handle == IntPtr.Zero)
            return;
        _handle = IntPtr.Zero;
        var rc = NativeMethods.zlink_spot_node_publisher_close(handle);
        if (rc != 0 && disposing)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    private delegate int NativePublisherSubmitter(IntPtr parts, nuint partCount);
}