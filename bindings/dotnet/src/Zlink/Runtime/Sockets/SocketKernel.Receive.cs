// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel
{
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
    internal unsafe bool ReceivePartInto(Message result, out bool hasMore,
        int flags)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));

        ZlinkMsg part = default;
        int initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        bool initialized = true;
        try
        {
            int rc = (flags & DontWaitFlag) != 0
                ? NativeMethods.zlink_recv_part_nowait(Handle,
                    out IntPtr sourceRoutingId, ref part, out int more,
                    flags)
                : NativeMethods.zlink_recv_part(Handle,
                    out sourceRoutingId, ref part, out more, flags);

            if (rc != 0)
            {
                NativeMethods.zlink_msg_close(ref part);
                initialized = false;
                int errno = NativeMethods.zlink_errno();
                if ((flags & DontWaitFlag) != 0
                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                        or ErrorCode.EBusy)
                {
                    hasMore = false;
                    return false;
                }

                throw ZlinkException.CreateRecvException(errno);
            }

            initialized = false;
            hasMore = more != 0;
            result.ReplaceNativeOwned(ref part);
            return true;
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref part);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal unsafe bool ReceiveRoutedPartInto(Message result,
        out RoutingId? routingId, out bool hasMore, int flags)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));

        routingId = null;
        ZlinkMsg part = default;
        int initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        bool initialized = true;
        try
        {
            int rc;
            int more;
            IntPtr sourceRoutingId;
            if (Type == SocketType.Router)
            {
                IntPtr sourceSpotRid;
                ulong requestSeq;
                rc = (flags & DontWaitFlag) != 0
                    ? NativeMethods.zlink_router_recv_part_nowait(Handle,
                        out sourceRoutingId, out sourceSpotRid,
                        out requestSeq, ref part, out more, flags)
                    : NativeMethods.zlink_router_recv_part(Handle,
                        out sourceRoutingId, out sourceSpotRid,
                        out requestSeq, ref part, out more, flags);
            }
            else
            {
                rc = (flags & DontWaitFlag) != 0
                    ? NativeMethods.zlink_recv_part_nowait(Handle,
                        out sourceRoutingId, ref part, out more, flags)
                    : NativeMethods.zlink_recv_part(Handle,
                        out sourceRoutingId, ref part, out more, flags);
            }
            hasMore = more != 0;

            if (rc != 0)
            {
                NativeMethods.zlink_msg_close(ref part);
                initialized = false;
                int errno = NativeMethods.zlink_errno();
                if ((flags & DontWaitFlag) != 0
                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                        or ErrorCode.EBusy)
                {
                    hasMore = false;
                    return false;
                }

                throw ZlinkException.CreateRecvException(errno);
            }

            initialized = false;
            routingId = RoutingIdSnapshot.FromPointer(sourceRoutingId)
                .ToRoutingId();
            result.ReplaceNativeOwned(ref part);
            return true;
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref part);
            throw;
        }
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
            SendReplyCore(replyRoutingId.Value, replySpotRid, requestSeq,
                replyParts, sendFlags);
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


}
