// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel
{
    private unsafe TopicMessage SubscribeCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            MultipartMessageCollection parts = ReceiveSubscribedParts(flags,
                topicBuffer, out byte[]? routingIdBytes, out string topic)
                ?? throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            if (parts.Count == 0)
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
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            bool allowNoData = (flags & DontWaitFlag) != 0;
            MultipartMessageCollection? parts = ReceiveSubscribedParts(flags,
                topicBuffer, out byte[]? routingIdBytes, out string topic,
                allowNoData: allowNoData);
            if (parts == null)
                return false;
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            if (parts.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            result.Populate(routingId, topic, parts);
            return true;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
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

    private unsafe Received ReceiveCore(int flags)
    {
        if (!ReceiveBasicParts(flags, out _, out Message? singlePart,
                out MultipartMessageCollection? parts))
        {
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        }
        return singlePart != null
            ? Received.Create((RoutingId?)null, singlePart)
            : Received.Create((RoutingId?)null, parts!);
    }

    private unsafe Received? TryReceiveMessageCore(int flags)
    {
        if (!ReceiveBasicParts(flags, out _, out Message? singlePart,
                out MultipartMessageCollection? parts, allowNoData: true))
        {
            return null;
        }
        return singlePart != null
            ? Received.Create((RoutingId?)null, singlePart)
            : Received.Create((RoutingId?)null, parts!);
    }

    private unsafe Received ReceiveRoutedCore(int flags)
    {
        if (!ReceiveRoutedParts(flags, out RoutingIdSnapshot routingId,
                out RoutingIdSnapshot spotRid, out ulong requestSeq,
                out Message? singlePart, out MultipartMessageCollection? parts))
        {
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        }
        return singlePart != null
            ? CreateRoutedReceived(singlePart, routingId, spotRid, requestSeq)
            : CreateRoutedReceived(parts!, routingId, spotRid, requestSeq);
    }

    private unsafe Received? TryReceiveRoutedCore(int flags)
    {
        if (!ReceiveRoutedParts(flags, out RoutingIdSnapshot routingId,
                out RoutingIdSnapshot spotRid, out ulong requestSeq,
                out Message? singlePart, out MultipartMessageCollection? parts,
                allowNoData: true))
        {
            return null;
        }
        return singlePart != null
            ? CreateRoutedReceived(singlePart, routingId, spotRid, requestSeq)
            : CreateRoutedReceived(parts!, routingId, spotRid, requestSeq);
    }

    // Canonical ref-out recv: caller provides Received storage and the
    // kernel rewrites its internal state on each successful receive. Avoids
    // the per-call Received allocation that the legacy Recv() returning
    // Received? incurs. See doc/spec/bindings/README.md
    // "Canonical Recv: Caller-Provided Storage".

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool ReceiveInto(Received result, int flags)
    {
        // The typed socket classes that expose Recv(Received, RecvFlags) on
        // their public surface only inherit from MessageSocketBase /
        // RoutedMessageSocketBase, so the message-receive capability is
        // already guaranteed by the class hierarchy. Mirror the legacy
        // RecvMessageNoWaitUnchecked fast path and skip the per-call
        // EnsureSupports lookup; this keeps DR/RR 64B-1024B recv at the
        // same hot-path cost as before the canonical migration.
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        return TryReceiveIntoMessageCore(result, flags);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool ReceiveRoutedInto(Received result, int flags)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        return TryReceiveIntoRoutedCore(result, flags);
    }

    private unsafe bool TryReceiveIntoMessageCore(Received result, int flags)
    {
        bool allowNoData = (flags & DontWaitFlag) != 0;
        if (!ReceiveBasicParts(flags, out _, out Message? singlePart,
                out MultipartMessageCollection? parts,
                allowNoData: allowNoData))
        {
            return false;
        }
        if (singlePart != null)
            result.PopulateSinglePart(singlePart);
        else
            result.PopulateMultipart(parts!);
        return true;
    }

    private unsafe bool TryReceiveIntoRoutedCore(Received result, int flags)
    {
        bool allowNoData = (flags & DontWaitFlag) != 0;
        if (!ReceiveRoutedParts(flags, out RoutingIdSnapshot routingId,
                out RoutingIdSnapshot spotRid, out ulong requestSeq,
                out Message? singlePart, out MultipartMessageCollection? parts,
                allowNoData: allowNoData))
        {
            return false;
        }
        PopulateRoutedReceivedInto(result, singlePart, parts, routingId,
            spotRid, requestSeq);
        return true;
    }

    private void PopulateRoutedReceivedInto(Received result,
        Message? singlePart, MultipartMessageCollection? parts,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid,
        ulong requestSeq)
    {
        if (requestSeq == 0)
        {
            if (singlePart != null)
                result.PopulateRoutedSinglePart(singlePart, routingId, spotRid,
                    null, null, sendKernel: this);
            else
                result.PopulateRoutedMultipart(parts!, routingId, spotRid,
                    null, null, sendKernel: this);
            return;
        }

        // Request-reply context: capture the routing ids and request seq in
        // a reply handler closure so Received.Reply() can dispatch via the
        // kernel. This path allocates a RoutingId / byte[] / closure per
        // recv; non-request-reply routed traffic (the common router-router
        // / dealer-router echo case) skips this branch entirely.
        byte[]? routingIdBytes = routingId.ToByteArray();
        byte[]? spotRidBytes = spotRid.ToByteArray();
        RoutingId? replyRoutingId = routingIdBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
        RoutingId? replySpotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        ReceivedReplyHandler replyHandler = (replyParts, sendFlags) =>
        {
            if (replyRoutingId is null)
                throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                    (int)ErrorCode.EInval);
            Message[] copied = new Message[replyParts.Count];
            for (int i = 0; i < copied.Length; i++)
                copied[i] = replyParts[i];
            SendReplyCore(replyRoutingId.Value, replySpotRid, requestSeq,
                copied, sendFlags);
        };

        if (singlePart != null)
            result.PopulateRoutedSinglePart(singlePart, routingId, spotRid,
                requestSeq, replyHandler, CreateRoutedSendHandler(routingId, spotRid),
                CreateRoutedSendSingleHandler(routingId, spotRid));
        else
            result.PopulateRoutedMultipart(parts!, routingId, spotRid,
                requestSeq, replyHandler, CreateRoutedSendHandler(routingId, spotRid),
                CreateRoutedSendSingleHandler(routingId, spotRid));
    }

    internal bool SendReceivedSingle(RoutingIdSnapshot routingId,
        RoutingIdSnapshot spotRid, Message part, SendFlags flags)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        if (!routingId.HasValue)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            RoutingId? target = routingId.ToRoutingId();
            if (!target.HasValue)
            {
                throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                    (int)ErrorCode.EInval);
            }
            return SendToSpotCore(target.Value, targetSpot.Value,
                new[] { part }, flags);
        }

        ZlinkRoutingId nativeRoutingId = default;
        routingId.WriteNative(ref nativeRoutingId);
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SendSingleResultCore(ref nativeRoutingId, part,
                (int)flags) == SendResult.Sent;
        }

        SendSingleCore(ref nativeRoutingId, part, (int)flags);
        return true;
    }

    internal bool SendReceivedParts(RoutingIdSnapshot routingId,
        RoutingIdSnapshot spotRid, IReadOnlyList<Message> parts, SendFlags flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

        RoutingId? target = routingId.ToRoutingId();
        if (!target.HasValue)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
            return SendToSpotCore(target.Value, targetSpot.Value, parts, flags);

        if ((flags & SendFlags.DontWait) != 0)
            return SendNoWaitResult(target.Value, parts) == SendResult.Sent;

        Send(target.Value, parts, flags);
        return true;
    }

    private ReceivedSendHandler? CreateRoutedSendHandler(
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid)
    {
        RoutingId? target = routingId.ToRoutingId();
        if (target == null)
            return null;
        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            RoutingId nodeRid = target.Value;
            RoutingId spotTarget = targetSpot.Value;
            return (sendParts, sendFlags) => SendToSpotCore(nodeRid, spotTarget,
                sendParts, sendFlags);
        }
        return (sendParts, sendFlags) =>
        {
            if ((sendFlags & SendFlags.DontWait) != 0)
            {
                return SendNoWaitResult(target.Value, sendParts) == SendResult.Sent;
            }
            Send(target.Value, sendParts, sendFlags);
            return true;
        };
    }

    private unsafe ReceivedSendSingleHandler? CreateRoutedSendSingleHandler(
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid)
    {
        if (!routingId.HasValue)
            return null;
        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            RoutingId? targetNode = routingId.ToRoutingId();
            if (!targetNode.HasValue)
                return null;
            RoutingId nodeRid = targetNode.Value;
            RoutingId spotTarget = targetSpot.Value;
            return (sendPart, sendFlags) => SendToSpotCore(nodeRid, spotTarget,
                new[] { sendPart }, sendFlags);
        }

        RoutingId? target = routingId.ToRoutingId();
        if (!target.HasValue)
            return null;
        RoutingId targetRid = target.Value;
        return (sendPart, sendFlags) =>
        {
            if ((sendFlags & SendFlags.DontWait) != 0)
            {
                return SendRoutedMessageResultUnchecked(targetRid, sendPart,
                    (int)sendFlags)
                    == SendResult.Sent;
            }
            SendRoutedMessageUnchecked(targetRid, sendPart, sendFlags);
            return true;
        };
    }

    private unsafe bool SendToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_router_send_spot_part(Handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        partFlag));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            RequestReplySupport.DisposeParts(cloned);
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe byte[][] ReceiveRawFramesCore(int flags)
    {
        return ReceiveRawFrameSequence(flags, includeRoutingFrames: true)
            .ToArray();
    }

    private unsafe int ReceiveRawFrameCore(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        List<byte[]> frames = ReceiveRawFrameSequence(flags,
            includeRoutingFrames: false);
        if (frames.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        return CopyFirstFrameAndCollectPending(frames, destination,
            out pendingFrames);
    }

    private unsafe int ReceiveRawRoutedFrameCore(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out int routingLength,
        out byte[][] pendingFrames)
    {
        List<byte[]> payloadFrames = ReceiveRawFrameSequence(flags,
            includeRoutingFrames: false, out byte[]? routingBytes,
            out _, out _);
        if (payloadFrames.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        routingLength = routingBytes == null
            ? 0
            : CopyRoutingId(routingBytes, routingDestination);
        return CopyFirstFrameAndCollectPending(payloadFrames, payloadDestination,
            out pendingFrames);
    }

}
