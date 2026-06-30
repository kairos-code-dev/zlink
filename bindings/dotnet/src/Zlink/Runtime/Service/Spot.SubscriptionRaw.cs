// SPDX-License-Identifier: MPL-2.0

using System.Buffers;
using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    private int ReceiveRawSubscribedFrameCore(Span<byte> destination,
        int flags, out byte[][] pendingFrames)
    {
        var topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            var frames = ReceiveSpotSubscribedFrames(flags, topicBuffer);
            if (frames.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return CopyFirstFrameAndCollectPending(frames, destination,
                out pendingFrames);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private MultipartMessageCollection? ReceiveSpotRoutedParts(int flags,
        out byte[]? nodeRidBytes, out byte[]? spotRidBytes,
        out ulong requestSeq, bool allowNoData = false)
    {
        var nativeParts = Array.Empty<ZlinkMsg>();
        var nativePartCount = 0;
        nodeRidBytes = null;
        spotRidBytes = null;
        requestSeq = 0;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                var initRc = NativeMethods.zlink_msg_init(ref part);
                if (initRc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                var initialized = true;
                var rc = NativeMethods.zlink_spot_recv_part(Handle,
                    out var sourceNodeRid, out var sourceSpotRid,
                    out requestSeq, ref part, out var hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    var errno = NativeMethods.zlink_errno();
                    if (allowNoData && nativePartCount == 0
                                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                                        or ErrorCode.EBusy)
                        return null;
                    throw ZlinkException.CreateRecvException(errno);
                }

                initialized = false;
                nodeRidBytes ??= CopyRoutingIdBytes(sourceNodeRid);
                spotRidBytes ??= CopyRoutingIdBytes(sourceSpotRid);
                AppendNativePart(ref nativeParts, ref nativePartCount, ref part);
                if (hasMore == 0)
                    break;
            }

            return MultipartMessageCollection.FromNativeParts(nativeParts,
                nativePartCount);
        }
        catch
        {
            CloseNativeParts(nativeParts, nativePartCount);
            throw;
        }
    }

    private bool ReceiveSpotSubscribedParts(
        int flags, byte[] topicBuffer, out RoutingIdSnapshot routingId,
        out int topicLength, out Message? singlePart,
        out MultipartMessageCollection? parts, bool allowNoData = false)
    {
        // HOT PATH: public Spot.Subscribe(TopicMessage, ...) reaches this method
        // for each subscribed message. Keep the single-part case on the direct
        // Message.AdoptNativeFromPool path; routing/topic decode and multipart
        // collection allocation belong only to the cases that actually need them.
        var nativeParts = Array.Empty<ZlinkMsg>();
        var nativePartCount = 0;
        routingId = default;
        topicLength = 0;
        singlePart = null;
        parts = null;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                var initRc = NativeMethods.zlink_msg_init(ref part);
                if (initRc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                var initialized = true;
                var rc = NativeMethods.zlink_spot_subscribe_part(Handle,
                    out var sourceRoutingId, topicBuffer,
                    (nuint)topicBuffer.Length, out var nativeTopicLength, ref part,
                    out var hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    var errno = NativeMethods.zlink_errno();
                    if (allowNoData && nativePartCount == 0
                                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                                        or ErrorCode.EBusy)
                        return false;
                    throw ZlinkException.CreateRecvException(errno);
                }

                initialized = false;
                if (nativePartCount == 0)
                {
                    routingId = RoutingIdSnapshot.FromPointer(sourceRoutingId);
                    topicLength = checked((int)nativeTopicLength);
                }

                if (hasMore == 0 && nativePartCount == 0)
                {
                    singlePart = Message.AdoptNativeFromPool(ref part);
                    return true;
                }

                AppendNativePart(ref nativeParts, ref nativePartCount, ref part);
                if (hasMore == 0)
                    break;
            }

            parts = MultipartMessageCollection.FromNativeParts(nativeParts,
                nativePartCount);
            return true;
        }
        catch
        {
            CloseNativeParts(nativeParts, nativePartCount);
            singlePart?.Dispose();
            throw;
        }
    }

    private List<byte[]> ReceiveSpotSubscribedFrames(int flags,
        byte[] topicBuffer)
    {
        List<byte[]> frames = new();
        while (true)
        {
            ZlinkMsg part = default;
            var initRc = NativeMethods.zlink_msg_init(ref part);
            if (initRc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            var initialized = true;
            var rc = NativeMethods.zlink_spot_subscribe_part(Handle,
                out _, topicBuffer, (nuint)topicBuffer.Length, out _, ref part,
                out var hasMore, flags);
            if (rc != 0)
            {
                if (initialized)
                    NativeMethods.zlink_msg_close(ref part);
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            }

            initialized = false;
            frames.Add(CopyAndClosePart(ref part));
            if (hasMore == 0)
                return frames;
        }
    }

    private unsafe void DiscardRemainingSubscribedParts(int flags)
    {
        var hasMore = 1;
        var topicBuffer = stackalloc byte[TopicBufferSize];
        while (hasMore != 0)
        {
            ZlinkMsg part = default;
            var initRc = NativeMethods.zlink_msg_init(ref part);
            if (initRc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            try
            {
                var rc = NativeMethods.zlink_spot_subscribe_part_buffer(
                    Handle, out _, topicBuffer, TopicBufferSize, out _,
                    ref part, out hasMore, flags);
                if (rc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
            }
            finally
            {
                NativeMethods.zlink_msg_close(ref part);
            }
        }
    }

    private static int CopyFirstFrameAndCollectPending(IReadOnlyList<byte[]> frames,
        Span<byte> destination, out byte[][] pendingFrames)
    {
        if (frames.Count == 0)
        {
            pendingFrames = Array.Empty<byte[]>();
            return 0;
        }

        var first = frames[0];
        if (first.Length > destination.Length)
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));

        first.AsSpan().CopyTo(destination);
        if (frames.Count <= 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return first.Length;
        }

        pendingFrames = new byte[frames.Count - 1][];
        for (var i = 1; i < frames.Count; i++)
            pendingFrames[i - 1] = frames[i];
        return first.Length;
    }

    private static unsafe byte[] CopyAndClosePart(ref ZlinkMsg part)
    {
        try
        {
            var size = checked((int)NativeMethods.zlink_msg_size(ref part));
            if (size == 0)
                return Array.Empty<byte>();

            var data = NativeMethods.zlink_msg_data(ref part);
            if (data == IntPtr.Zero)
                return Array.Empty<byte>();

            var payload = new byte[size];
            new ReadOnlySpan<byte>((void*)data, size).CopyTo(payload);
            return payload;
        }
        finally
        {
            NativeMethods.zlink_msg_close(ref part);
        }
    }

    private static ZlinkMsg MoveStoredPart(ref ZlinkMsg source)
    {
        ZlinkMsg stored = default;
        var initRc = NativeMethods.zlink_msg_init(ref stored);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        try
        {
            var rc = NativeMethods.zlink_msg_move(ref stored, ref source);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            return stored;
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref stored);
            throw;
        }
    }

    private static void AppendNativePart(ref ZlinkMsg[] nativeParts,
        ref int count, ref ZlinkMsg source)
    {
        if (count == nativeParts.Length)
            Array.Resize(ref nativeParts, count == 0 ? 4 : count * 2);
        nativeParts[count++] = MoveStoredPart(ref source);
    }

    private static void CloseNativeParts(ZlinkMsg[] parts, int count)
    {
        for (var i = 0; i < count; i++)
            NativeMethods.zlink_msg_close(ref parts[i]);
    }

    private static unsafe byte[]? CopyRoutingIdBytes(IntPtr routingIdPtr)
    {
        if (routingIdPtr == IntPtr.Zero)
            return null;
        return NativeHelpers.ReadRoutingId(ref *(ZlinkRoutingId*)routingIdPtr);
    }

    private static string DecodeBuffer(byte[] buffer, nuint length)
    {
        var boundedLength = length > (nuint)buffer.Length
            ? buffer.Length
            : (int)length;
        return boundedLength == 0
            ? string.Empty
            : Encoding.UTF8.GetString(buffer, 0, boundedLength);
    }
}