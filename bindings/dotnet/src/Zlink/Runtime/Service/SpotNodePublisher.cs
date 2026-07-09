// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class SpotNodePublisher : ISpotNodePublisher
{
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

    private static void SubmitParts(IReadOnlyList<Message> parts,
        NativePublisherSubmitter submit)
    {
        NativeMessageParts.SubmitClonedVector(parts, nameof(parts),
            (nativeParts, partCount) => submit(nativeParts, partCount),
            ZlinkException.ThrowSubmitIfError);
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
