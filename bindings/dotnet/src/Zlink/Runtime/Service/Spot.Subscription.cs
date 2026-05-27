// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Text;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    private unsafe TopicMessage SubscribeCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            if (!ReceiveSpotSubscribedParts(flags, topicBuffer,
                    out RoutingIdSnapshot routingId, out int topicLength,
                    out Message? singlePart, out MultipartMessageCollection? parts))
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            string topic = DecodeBuffer(topicBuffer, (nuint)topicLength);
            if (singlePart != null)
                return new TopicMessage(routingId, topic, singlePart);
            if (parts == null || parts.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return new TopicMessage(routingId, topic, parts);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe bool SubscribeInto(TopicMessage result, int flags)
    {
        byte[] topicBuffer = result.GetWritableTopicBuffer(TopicBufferSize);
        bool received = ReceiveSpotSubscribedParts(flags,
            topicBuffer, out RoutingIdSnapshot routingId, out int topicLength,
            out Message? singlePart, out MultipartMessageCollection? parts,
            allowNoData: (flags & 1) != 0);
        if (!received)
            return false;
        if (singlePart != null)
        {
            result.PopulateSinglePartFromWritableTopicBuffer(routingId,
                topicLength, singlePart);
            return true;
        }
        if (parts == null || parts.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        result.PopulateFromWritableTopicBuffer(routingId, topicLength, parts);
        return true;
    }

    private unsafe bool SubscribePartInto(Message result, Span<byte> topicBuffer,
        out int topicLength, out bool hasMore, int flags)
    {
        topicLength = 0;
        hasMore = false;
        ZlinkMsg part = default;
        int initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        bool initialized = true;
        try
        {
            fixed (byte* topicPtr = topicBuffer)
            {
                int rc = NativeMethods.zlink_spot_subscribe_part_buffer(
                    _handle, out _, topicPtr, (nuint)topicBuffer.Length,
                    out nuint nativeTopicLength, ref part,
                    out int nativeHasMore, flags);
                if (rc != 0)
                {
                    int errno = NativeMethods.zlink_errno();
                    if ((flags & DontWaitFlag) != 0
                        && ZlinkException.MapErrorCode(errno)
                            is ErrorCode.EAgain or ErrorCode.EBusy)
                    {
                        return false;
                    }
                    throw ZlinkException.CreateRecvException(errno);
                }

                topicLength = checked((int)nativeTopicLength);
                hasMore = nativeHasMore != 0;
                result.ReplaceNativeOwned(ref part);
                initialized = false;
                return true;
            }
        }
        finally
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref part);
        }
    }

    private unsafe SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            int rc = NativeMethods.zlink_spot_subscription_event_recv(_handle,
                out IntPtr sourceRoutingId, out int subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
            {
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            }

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            return new SubscriptionEvent(routingId, topic, subscribedInt != 0);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe bool ReceiveSubscriptionEventInto(SubscriptionEvent result,
        int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            int rc = NativeMethods.zlink_spot_subscription_event_recv(_handle,
                out IntPtr sourceRoutingId, out int subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
            {
                int errno = NativeMethods.zlink_errno();
                if ((flags & 1) != 0
                    && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                    return false;
                throw ZlinkException.CreateRecvException(errno);
            }

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            result.Populate(routingId, topic, subscribedInt != 0);
            return true;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SendResult PublishNoWaitSingleCore(string topic,
        Message message)
    {
        return PublishNoWaitSingleCore(GetPublishTopicUtf8(topic), message);
    }

    private unsafe SendResult PublishNoWaitSingleCore(byte[] topicUtf8,
        Message message)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            fixed (byte* topicPtr = topicUtf8)
            {
                int rc = NativeMethods.zlink_spot_publish_part_utf8(_handle,
                    topicPtr, ref nativePart, DontWaitFlag,
                    NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc == 0)
                    return SendResult.Sent;
            }

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return sendResult.Value;
        }
        catch
        {
            if (!submitted)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe int ReceiveRawSubscribedFrameCore(Span<byte> destination,
        int flags, out byte[][] pendingFrames)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            List<byte[]> frames = ReceiveSpotSubscribedFrames(flags, topicBuffer);
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

    private unsafe MultipartMessageCollection? ReceiveSpotRoutedParts(int flags,
        out byte[]? nodeRidBytes, out byte[]? spotRidBytes,
        out ulong requestSeq, bool allowNoData = false)
    {
        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
        nodeRidBytes = null;
        spotRidBytes = null;
        requestSeq = 0;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                if (initRc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                bool initialized = true;
                int rc = NativeMethods.zlink_spot_recv_part(_handle,
                    out IntPtr sourceNodeRid, out IntPtr sourceSpotRid,
                    out requestSeq, ref part, out int hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    int errno = NativeMethods.zlink_errno();
                    if (allowNoData && nativePartCount == 0
                        && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                            or ErrorCode.EBusy)
                    {
                        return null;
                    }
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

    private unsafe bool ReceiveSpotSubscribedParts(
        int flags, byte[] topicBuffer, out RoutingIdSnapshot routingId,
        out int topicLength, out Message? singlePart,
        out MultipartMessageCollection? parts, bool allowNoData = false)
    {
        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
        routingId = default;
        topicLength = 0;
        singlePart = null;
        parts = null;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                if (initRc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                bool initialized = true;
                int rc = NativeMethods.zlink_spot_subscribe_part(_handle,
                    out IntPtr sourceRoutingId, topicBuffer,
                    (nuint)topicBuffer.Length, out nuint nativeTopicLength, ref part,
                    out int hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    int errno = NativeMethods.zlink_errno();
                    if (allowNoData && nativePartCount == 0
                        && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                            or ErrorCode.EBusy)
                    {
                        return false;
                    }
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

    private unsafe List<byte[]> ReceiveSpotSubscribedFrames(int flags,
        byte[] topicBuffer)
    {
        List<byte[]> frames = new();
        while (true)
        {
            ZlinkMsg part = default;
            int initRc = NativeMethods.zlink_msg_init(ref part);
            if (initRc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            bool initialized = true;
            int rc = NativeMethods.zlink_spot_subscribe_part(_handle,
                out _, topicBuffer, (nuint)topicBuffer.Length, out _, ref part,
                out int hasMore, flags);
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
        int hasMore = 1;
        byte* topicBuffer = stackalloc byte[TopicBufferSize];
        while (hasMore != 0)
        {
            ZlinkMsg part = default;
            int initRc = NativeMethods.zlink_msg_init(ref part);
            if (initRc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            try
            {
                int rc = NativeMethods.zlink_spot_subscribe_part_buffer(
                    _handle, out _, topicBuffer, TopicBufferSize, out _,
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

        byte[] first = frames[0];
        if (first.Length > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        first.AsSpan().CopyTo(destination);
        if (frames.Count <= 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return first.Length;
        }

        pendingFrames = new byte[frames.Count - 1][];
        for (int i = 1; i < frames.Count; i++)
            pendingFrames[i - 1] = frames[i];
        return first.Length;
    }

    private static unsafe byte[] CopyAndClosePart(ref ZlinkMsg part)
    {
        try
        {
            int size = checked((int)NativeMethods.zlink_msg_size(ref part));
            if (size == 0)
                return Array.Empty<byte>();

            IntPtr data = NativeMethods.zlink_msg_data(ref part);
            if (data == IntPtr.Zero)
                return Array.Empty<byte>();

            byte[] payload = new byte[size];
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
        int initRc = NativeMethods.zlink_msg_init(ref stored);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        try
        {
            int rc = NativeMethods.zlink_msg_move(ref stored, ref source);
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

    private static unsafe void CloseNativeParts(ZlinkMsg[] parts, int count)
    {
        for (int i = 0; i < count; i++)
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
        int boundedLength = length > (nuint)buffer.Length
            ? buffer.Length
            : (int)length;
        return boundedLength == 0
            ? string.Empty
            : Encoding.UTF8.GetString(buffer, 0, boundedLength);
    }

}
