// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    private unsafe TopicMessage SubscribeCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            if (!ReceiveSubscribedParts(flags, topicBuffer,
                    out RoutingIdSnapshot routingId, out int topicLength,
                    out Message? singlePart, out MultipartMessageCollection? parts))
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            string topic = DecodeTopic(topicBuffer, (nuint)topicLength);
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
        bool allowNoData = (flags & DontWaitFlag) != 0;
        bool received = ReceiveSubscribedParts(flags, topicBuffer,
            out RoutingIdSnapshot routingId, out int topicLength,
            out Message? singlePart, out MultipartMessageCollection? parts,
            allowNoData: allowNoData);
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool SubscribeIntoSubscriber(TopicMessage result, int flags)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        try
        {
            return SubscribeInto(result, flags);
        }
        catch (ZlinkException ex) when ((flags & DontWaitFlag) != 0
            && ZlinkException.MapErrorCode(ex.InternalErrno) is ErrorCode.EAgain
                or ErrorCode.EBusy)
        {
            return false;
        }
    }

    private unsafe byte[][] ReceiveRawSubscribedFramesCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            List<byte[]> frames = ReceiveSubscribedFrames(flags, topicBuffer);
            if (frames.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return frames.ToArray();
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe int ReceiveRawSubscribedFrameCore(Span<byte> destination,
        int flags, out byte[][] pendingFrames)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            List<byte[]> frames = ReceiveSubscribedFrames(flags, topicBuffer);
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

    private unsafe SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            int rc = NativeMethods.zlink_xpub_recv_part(Handle,
                out IntPtr sourceRoutingId, out int subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string topic = DecodeTopic(topicBuffer, topicLength);
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
            int rc = NativeMethods.zlink_xpub_recv_part(Handle,
                out IntPtr sourceRoutingId, out int subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
            {
                int errno = NativeMethods.zlink_errno();
                if ((flags & DontWaitFlag) != 0
                    && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                    return false;
                throw ZlinkException.CreateRecvException(errno);
            }

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string topic = DecodeTopic(topicBuffer, topicLength);
            result.Populate(routingId, topic, subscribedInt != 0);
            return true;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }
}
