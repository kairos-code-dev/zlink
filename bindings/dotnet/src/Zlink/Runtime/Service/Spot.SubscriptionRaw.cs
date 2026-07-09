// SPDX-License-Identifier: MPL-2.0

using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
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
