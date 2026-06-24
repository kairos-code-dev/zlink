// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel
{
    private unsafe bool ReceiveBasicParts(int flags,
        out byte[]? routingIdBytes, out Message? singlePart,
        out MultipartMessageCollection? parts, bool allowNoData = false)
    {
        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
        routingIdBytes = null;
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
                int rc = (flags & DontWaitFlag) != 0
                    ? NativeMethods.zlink_recv_part_nowait(Handle,
                        out IntPtr sourceRoutingId, ref part, out int hasMore,
                        flags)
                    : NativeMethods.zlink_recv_part(Handle,
                        out sourceRoutingId, ref part, out hasMore, flags);
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
                routingIdBytes ??= CopyRoutingIdBytes(sourceRoutingId);
                if (hasMore == 0 && nativePartCount == 0)
                {
                    // Pool-aware adoption: in routed echo workloads the
                    // Message wrapper lifetime is bounded by the caller's
                    // using-scope. Recycling these instances eliminates a
                    // per-message heap allocation and Gen 0 GC pressure.
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

    private unsafe bool ReceiveRoutedParts(int flags,
        out RoutingIdSnapshot routingId, out RoutingIdSnapshot spotRid,
        out ulong requestSeq, out Message? singlePart,
        out MultipartMessageCollection? parts, bool allowNoData = false)
    {
        routingId = default;
        spotRid = default;
        requestSeq = 0;
        singlePart = null;
        parts = null;
        if (Type == SocketType.Router)
        {
            return ReceiveRouterParts(flags, out routingId, out spotRid,
                out requestSeq, out singlePart,
                out parts, allowNoData);
        }

        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
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
                int rc;
                IntPtr sourceNodeRid;
                rc = NativeMethods.zlink_recv_part(Handle, out sourceNodeRid,
                    ref part, out int basicHasMore, flags);
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
                if (!routingId.HasValue)
                    routingId = RoutingIdSnapshot.FromPointer(sourceNodeRid);
                if (basicHasMore == 0 && nativePartCount == 0)
                {
                    singlePart = Message.AdoptNativeFromPool(ref part);
                    return true;
                }

                AppendNativePart(ref nativeParts, ref nativePartCount, ref part);
                if (basicHasMore == 0)
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

    private unsafe bool ReceiveRouterParts(int flags,
        out RoutingIdSnapshot routingId, out RoutingIdSnapshot spotRid,
        out ulong requestSeq, out Message? singlePart,
        out MultipartMessageCollection? parts, bool allowNoData)
    {
        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
        routingId = default;
        spotRid = default;
        requestSeq = 0;
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
                // DONT_WAIT-only variant: avoid blocking while still allowing
                // managed free callbacks during native message handling.
                IntPtr sourceNodeRid;
                IntPtr sourceSpotRid;
                ulong receivedRequestSeq;
                int hasMore;
                int rc = (flags & DontWaitFlag) != 0
                    ? NativeMethods.zlink_router_recv_part_nowait(Handle,
                        out sourceNodeRid, out sourceSpotRid,
                        out receivedRequestSeq, ref part, out hasMore,
                        flags)
                    : NativeMethods.zlink_router_recv_part(Handle,
                        out sourceNodeRid, out sourceSpotRid,
                        out receivedRequestSeq, ref part, out hasMore,
                        flags);
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
                    routingId = RoutingIdSnapshot.FromPointer(sourceNodeRid);
                    spotRid = RoutingIdSnapshot.FromPointer(sourceSpotRid);
                    requestSeq = receivedRequestSeq;
                }
                if (hasMore == 0 && nativePartCount == 0)
                {
                    // Pool-aware adoption: in routed echo workloads the
                    // Message wrapper lifetime is bounded by the caller's
                    // using-scope. Recycling these instances eliminates a
                    // per-message heap allocation and Gen 0 GC pressure.
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

    private unsafe bool ReceiveSubscribedParts(int flags,
        byte[] topicBuffer, out RoutingIdSnapshot routingId, out int topicLength,
        out Message? singlePart, out MultipartMessageCollection? parts,
        bool allowNoData = false)
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
                int rc = NativeMethods.zlink_subscribe_part(Handle,
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

    private unsafe List<byte[]> ReceiveSubscribedFrames(int flags, byte[] topicBuffer)
    {
        List<byte[]> frames = new();
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
                int rc = NativeMethods.zlink_subscribe_part(Handle,
                    out _, topicBuffer, (nuint)topicBuffer.Length,
                    out _, ref part, out int hasMore, flags);
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
                    break;
            }

            return frames;
        }
        catch
        {
            throw;
        }
    }

    private unsafe List<byte[]> ReceiveRawFrameSequence(int flags,
        bool includeRoutingFrames)
    {
        return ReceiveRawFrameSequence(flags, includeRoutingFrames,
            out _, out _, out _);
    }

    private unsafe List<byte[]> ReceiveRawFrameSequence(int flags,
        bool includeRoutingFrames, out byte[]? routingIdBytes,
        out byte[]? spotRidBytes, out ulong requestSeq)
    {
        List<byte[]> frames = new();
        routingIdBytes = null;
        spotRidBytes = null;
        requestSeq = 0;
        while (true)
        {
            ZlinkMsg part = default;
            int initRc = NativeMethods.zlink_msg_init(ref part);
            if (initRc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            bool initialized = true;
            int rc;
            IntPtr sourceNodeRid;
            IntPtr sourceSpotRid = IntPtr.Zero;
            int hasMore;
            if (Type == SocketType.Router)
            {
                rc = NativeMethods.zlink_router_recv_part(Handle,
                    out sourceNodeRid, out sourceSpotRid, out requestSeq,
                    ref part, out hasMore, flags);
            }
            else
            {
                rc = NativeMethods.zlink_recv_part(Handle, out sourceNodeRid,
                    ref part, out hasMore, flags);
            }

            if (rc != 0)
            {
                if (initialized)
                    NativeMethods.zlink_msg_close(ref part);
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            }

            initialized = false;
            if (routingIdBytes == null)
            {
                routingIdBytes = CopyRoutingIdBytes(sourceNodeRid);
                spotRidBytes = CopyRoutingIdBytes(sourceSpotRid);
                if (includeRoutingFrames)
                {
                    if (routingIdBytes != null)
                        frames.Add(routingIdBytes);
                    if (spotRidBytes != null)
                        frames.Add(spotRidBytes);
                }
            }

            frames.Add(CopyAndClosePart(ref part));
            if (hasMore == 0)
                return frames;
        }
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
        {
            Array.Resize(ref nativeParts, count == 0 ? 4 : count * 2);
        }

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

    private static unsafe bool TryReadRoutingId(IntPtr routingIdPtr,
        out ZlinkRoutingId routingId)
    {
        routingId = default;
        if (routingIdPtr == IntPtr.Zero)
            return false;

        routingId = *(ZlinkRoutingId*)routingIdPtr;
        return routingId.Size > 0;
    }

    private static byte[]? ToRoutingBytes(in ZlinkRoutingId routingId,
        bool hasRoutingId)
    {
        if (!hasRoutingId)
            return null;
        ZlinkRoutingId copy = routingId;
        return NativeHelpers.ReadRoutingId(ref copy);
    }

    private static unsafe int CopyRoutingId(ref ZlinkRoutingId routingId,
        Span<byte> destination)
    {
        int size = routingId.Size;
        if (size > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        if (size <= 0)
            return 0;

        fixed (byte* src = routingId.Data)
            new ReadOnlySpan<byte>(src, size).CopyTo(destination);
        return size;
    }

    private static int CopyRoutingId(ReadOnlySpan<byte> routingId,
        Span<byte> destination)
    {
        if (routingId.Length > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        routingId.CopyTo(destination);
        return routingId.Length;
    }

    private static T? TryReceiveCore<T>(Func<T> operation) where T : class
    {
        try
        {
            return operation();
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
            return null;
        }
    }

    private static bool MapTryReceiveableError(ZlinkException ex)
    {
        ErrorCode code = ZlinkException.MapErrorCode(ex.NativeErrno);
        return code == ErrorCode.EAgain;
    }

    private static string DecodeTopic(byte[] topicBuffer, nuint topicLength)
    {
        int boundedLength = topicLength > (nuint)topicBuffer.Length
            ? topicBuffer.Length
            : (int)topicLength;
        return boundedLength == 0
            ? string.Empty
            : Encoding.UTF8.GetString(topicBuffer, 0, boundedLength);
    }
}
