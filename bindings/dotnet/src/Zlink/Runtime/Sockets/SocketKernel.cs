// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    private const int StackSendPartLimit = 8;
    private const int TopicBufferSize = 4096;
    private const int DontWaitFlag = 1;

    private readonly SocketHandle _handle;
    private readonly SocketOptionAccessor _options;
    private readonly SocketTypePolicy _policy;
    private readonly SocketCallbackRegistry _callbacks = new();
    private string? _publishTopicCacheKey;
    private byte[]? _publishTopicCacheUtf8;
    private bool _streamAttached;
    private bool _discoveryAttached;

    public SocketKernel(Context context, SocketType type)
    {
        _handle = new SocketHandle(context, type);
        _options = new SocketOptionAccessor(() => Handle);
        _policy = new SocketTypePolicy(type);
    }

    public SocketKernel(IntPtr handle, bool own)
    {
        _handle = new SocketHandle(handle, own);
        _options = new SocketOptionAccessor(() => Handle);
        _policy = new SocketTypePolicy(
            SocketOptionAccessor.ReadSocketType(_handle.DangerousGetHandle()));
    }

    public IntPtr Handle => _handle.DangerousGetHandle();
    public SocketType Type => _policy.SocketType;

    public bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(Subscribe),
            SocketTypePolicy.SocketCapability.SubscribeReceive);
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        try
        {
            return SubscribeInto(result, (int)flags);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(ex.NativeErrno) is ErrorCode.EAgain
                or ErrorCode.EBusy)
        {
            return false;
        }
    }

    internal bool SubscribeNoWait(TopicMessage result)
    {
        return Subscribe(result, RecvFlags.DontWait);
    }

    internal byte[][]? TryReceiveRawSubscribedFrames(int flags)
    {
        EnsureSupports(nameof(TryReceiveRawSubscribedFrames),
            SocketTypePolicy.SocketCapability.SubscribeReceive);
        try
        {
            return ReceiveRawSubscribedFramesCore(flags);
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
            return null;
        }
    }

    internal int? TryReceiveRawSubscribedFrame(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        EnsureSupports(nameof(TryReceiveRawSubscribedFrame),
            SocketTypePolicy.SocketCapability.SubscribeReceive);
        try
        {
            return ReceiveRawSubscribedFrameCore(destination, flags,
                out pendingFrames);
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
            pendingFrames = Array.Empty<byte[]>();
            return null;
        }
    }

    public unsafe bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(ReceiveSubscriptionEvent),
            SocketTypePolicy.SocketCapability.SubscriptionEvents);
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        return ReceiveSubscriptionEventInto(result, (int)flags);
    }

    public bool ReceiveSubscriptionEventNoWait(SubscriptionEvent result)
    {
        return ReceiveSubscriptionEvent(result, RecvFlags.DontWait);
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(Recv),
            SocketTypePolicy.SocketCapability.MessageReceive);
        return RecvMessageUnchecked(flags);
    }

    internal Received RecvMessageUnchecked(RecvFlags flags = RecvFlags.None)
    {
        if ((((int)flags) & DontWaitFlag) != 0)
        {
            return TryReceiveMessageCore(DontWaitFlag)
                ?? throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        }
        return ReceiveCore((int)flags);
    }

    public Received? RecvNoWait()
    {
        EnsureSupports(nameof(RecvNoWait),
            SocketTypePolicy.SocketCapability.MessageReceive);
        return RecvMessageNoWaitUnchecked();
    }

    internal Received? RecvMessageNoWaitUnchecked()
    {
        return TryReceiveMessageCore(DontWaitFlag);
    }

    public Received ReceiveRouted(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(ReceiveRouted),
            SocketTypePolicy.SocketCapability.RoutedReceive);
        return ReceiveRoutedUnchecked(flags);
    }

    internal Received ReceiveRoutedUnchecked(RecvFlags flags = RecvFlags.None)
    {
        if ((((int)flags) & DontWaitFlag) != 0)
        {
            return TryReceiveRoutedCore(DontWaitFlag)
                ?? throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        }
        return ReceiveRoutedCore((int)flags);
    }

    public Received? ReceiveRoutedNoWait()
    {
        EnsureSupports(nameof(ReceiveRoutedNoWait),
            SocketTypePolicy.SocketCapability.RoutedReceive);
        return ReceiveRoutedNoWaitUnchecked();
    }

    internal Received? ReceiveRoutedNoWaitUnchecked()
    {
        return TryReceiveRoutedCore(DontWaitFlag);
    }

    internal byte[][]? TryReceiveRawFrames(int flags)
    {
        if (Type == SocketType.Router || Type == SocketType.Stream)
        {
            EnsureSupports(nameof(TryReceiveRawFrames),
                SocketTypePolicy.SocketCapability.RoutedReceive);
        }
        else
        {
            EnsureSupports(nameof(TryReceiveRawFrames),
                SocketTypePolicy.SocketCapability.MessageReceive);
        }

        try
        {
            return ReceiveRawFramesCore(flags);
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
            return null;
        }
    }

    internal int? TryReceiveRawFrame(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        EnsureSupports(nameof(TryReceiveRawFrame),
            SocketTypePolicy.SocketCapability.MessageReceive);
        try
        {
            return ReceiveRawFrameCore(destination, flags, out pendingFrames);
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
            pendingFrames = Array.Empty<byte[]>();
            return null;
        }
    }

    internal int? TryReceiveRawRoutedFrame(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out byte[][] pendingFrames)
    {
        int routingLength;
        return TryReceiveRawRoutedFrame(routingDestination, payloadDestination,
            flags, out routingLength, out pendingFrames);
    }

    internal int? TryReceiveRawRoutedFrame(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out int routingLength,
        out byte[][] pendingFrames)
    {
        EnsureSupports(nameof(TryReceiveRawRoutedFrame),
            SocketTypePolicy.SocketCapability.RoutedReceive);
        try
        {
            return ReceiveRawRoutedFrameCore(routingDestination,
                payloadDestination, flags, out routingLength, out pendingFrames);
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
            routingLength = 0;
            pendingFrames = Array.Empty<byte[]>();
            return null;
        }
    }


    private unsafe void SendReplyCore(RoutingId routingId,
        RoutingId? spotRid, ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        _ = flags;
        ZlinkRoutingId nativeRoutingId = routingId.ToNative();
        bool hasSpotRid = spotRid.HasValue;
        ZlinkRoutingId nativeSpotRid = default;
        if (hasSpotRid)
            nativeSpotRid = spotRid.GetValueOrDefault().ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) => !hasSpotRid
                    ? NativeMethods.zlink_router_reply_part(Handle,
                        ref nativeRoutingId, requestSeq, ref nativePart,
                        partFlag)
                    : NativeMethods.zlink_router_reply_spot_part(Handle,
                        ref nativeRoutingId, ref nativeSpotRid, requestSeq,
                        ref nativePart, partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private void SendPartsWithFlags(IReadOnlyList<Message> parts, int flags)
    {
        if (parts is Message[] array)
        {
            SendCore(array.AsSpan(), flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            SendCore(CollectionsMarshal.AsSpan(list), flags, nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        SendCore(copied.AsSpan(), flags, nameof(parts));
    }

    private SendResult SendPartsNoWaitResult(IReadOnlyList<Message> parts)
    {
        if (parts is Message[] array)
            return SendNoWaitResultCore(array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
            return SendNoWaitResultCore(CollectionsMarshal.AsSpan(list), nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return SendNoWaitResultCore(copied.AsSpan(), nameof(parts));
    }

    private void SendRoutedSingleWithFlags(string routingId, Message message,
        int flags)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendSingleCore(ref nativeRoutingId, message, flags);
    }

    private SendResult SendRoutedSingleNoWaitResult(string routingId,
        Message message)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return SendSingleNoWaitResultCore(ref nativeRoutingId, message);
    }

    private void SendRoutedPartsWithFlags(string routingId,
        IReadOnlyList<Message> parts, int flags)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);

        if (parts is Message[] array)
        {
            SendCore(ref nativeRoutingId, array.AsSpan(), flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            SendCore(ref nativeRoutingId, CollectionsMarshal.AsSpan(list), flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        SendCore(ref nativeRoutingId, copied.AsSpan(), flags, nameof(parts));
    }

    private SendResult SendRoutedPartsNoWaitResult(string routingId,
        IReadOnlyList<Message> parts)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);

        if (parts is Message[] array)
            return SendNoWaitResultCore(ref nativeRoutingId, array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
        {
            return SendNoWaitResultCore(ref nativeRoutingId,
                CollectionsMarshal.AsSpan(list), nameof(parts));
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return SendNoWaitResultCore(ref nativeRoutingId, copied.AsSpan(), nameof(parts));
    }

    private void PublishPartsWithFlags(string topic, IReadOnlyList<Message> parts,
        int flags)
    {
        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), flags, nameof(parts));
    }

    private SendResult PublishNoWaitParts(string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts is Message[] array)
            return PublishNoWaitCore(topic, array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
            return PublishNoWaitCore(topic, CollectionsMarshal.AsSpan(list),
                nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitCore(topic, copied.AsSpan(), nameof(parts));
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
        int firstSize = first.Length;
        if (firstSize > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        first.AsSpan().CopyTo(destination);

        if (frames.Count == 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return firstSize;
        }

        pendingFrames = new byte[frames.Count - 1][];
        for (int i = 1; i < frames.Count; i++)
            pendingFrames[i - 1] = frames[i];

        return firstSize;
    }

    private Received CreateRoutedReceived(MultipartMessageCollection parts,
        byte[]? routingIdBytes, byte[]? spotRidBytes, ulong requestSeq)
    {
        if (requestSeq == 0)
        {
            Received received = Received.Create(routingIdBytes, parts,
                adoptRoutingBytes: true, spotRidBytes: spotRidBytes);
            RoutingIdSnapshot routingId = RoutingIdSnapshot.FromBytes(routingIdBytes);
            RoutingIdSnapshot spotRid = RoutingIdSnapshot.FromBytes(spotRidBytes);
            received.SetSendHandler(CreateRoutedSendHandler(routingId, spotRid),
                CreateRoutedSendSingleHandler(routingId, spotRid));
            return received;
        }

        RoutingId? replyRoutingId = routingIdBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
        RoutingId? replySpotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        Received request = Received.Create(replyRoutingId, parts, requestSeq,
            replySpotRid, replyHandler: (replyParts, sendFlags) =>
            {
                if (replyRoutingId is null)
                {
                    throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument,
                        (int)ErrorCode.EInval);
                }

                SendReplyCore(replyRoutingId.Value, replySpotRid, requestSeq,
                    replyParts, sendFlags);
            });
        RoutingIdSnapshot requestRoutingId =
            RoutingIdSnapshot.FromBytes(routingIdBytes);
        RoutingIdSnapshot requestSpotRid =
            RoutingIdSnapshot.FromBytes(spotRidBytes);
        request.SetSendHandler(CreateRoutedSendHandler(requestRoutingId,
                requestSpotRid),
            CreateRoutedSendSingleHandler(requestRoutingId, requestSpotRid));
        return request;
    }

    private Received CreateRoutedReceived(Message singlePart,
        byte[]? routingIdBytes, byte[]? spotRidBytes, ulong requestSeq)
    {
        if (requestSeq == 0)
        {
            Received received = Received.Create(routingIdBytes, singlePart,
                adoptRoutingBytes: true, spotRidBytes: spotRidBytes);
            RoutingIdSnapshot routingId = RoutingIdSnapshot.FromBytes(routingIdBytes);
            RoutingIdSnapshot spotRid = RoutingIdSnapshot.FromBytes(spotRidBytes);
            received.SetSendHandler(CreateRoutedSendHandler(routingId, spotRid),
                CreateRoutedSendSingleHandler(routingId, spotRid));
            return received;
        }

        RoutingId? replyRoutingId = routingIdBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
        RoutingId? replySpotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        Received request = Received.Create(replyRoutingId, singlePart, requestSeq,
            replySpotRid, replyHandler: (replyParts, sendFlags) =>
            {
                if (replyRoutingId is null)
                {
                    throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument,
                        (int)ErrorCode.EInval);
                }

                SendReplyCore(replyRoutingId.Value, replySpotRid, requestSeq,
                    replyParts, sendFlags);
            });
        RoutingIdSnapshot requestRoutingId =
            RoutingIdSnapshot.FromBytes(routingIdBytes);
        RoutingIdSnapshot requestSpotRid =
            RoutingIdSnapshot.FromBytes(spotRidBytes);
        request.SetSendHandler(CreateRoutedSendHandler(requestRoutingId,
                requestSpotRid),
            CreateRoutedSendSingleHandler(requestRoutingId, requestSpotRid));
        return request;
    }

    private Received CreateRoutedReceived(MultipartMessageCollection parts,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid,
        ulong requestSeq)
    {
        if (requestSeq == 0)
        {
            Received received = Received.Create(routingId, parts,
                spotRid: spotRid);
            received.SetSendHandler(CreateRoutedSendHandler(routingId, spotRid),
                CreateRoutedSendSingleHandler(routingId, spotRid));
            return received;
        }

        byte[]? routingIdBytes = routingId.ToByteArray();
        byte[]? spotRidBytes = spotRid.ToByteArray();
        return CreateRoutedReceived(parts, routingIdBytes, spotRidBytes,
            requestSeq);
    }

    private Received CreateRoutedReceived(Message singlePart,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid,
        ulong requestSeq)
    {
        if (requestSeq == 0)
        {
            return Received.Create(routingId, singlePart, spotRid: spotRid);
        }

        byte[]? routingIdBytes = routingId.ToByteArray();
        byte[]? spotRidBytes = spotRid.ToByteArray();
        return CreateRoutedReceived(singlePart, routingIdBytes, spotRidBytes,
            requestSeq);
    }

    private static RoutingId ParsePublicRoutingId(string value)
    {
        const string hexPrefix = "hex:";
        return value.StartsWith(hexPrefix, StringComparison.Ordinal)
            ? RoutingId.FromHex(value.Substring(hexPrefix.Length))
            : RoutingId.From(value);
    }

    private static SendResult MapSendResult(int rc)
    {
        return rc switch
        {
            0 => SendResult.Sent,
            1 => SendResult.Backpressured,
            2 => SendResult.NotReady,
            _ => throw new InvalidOperationException(
                $"Unexpected send result code '{rc}'.")
        };
    }

    internal static bool TrySendOrThrow(SendResult result)
    {
        return result switch
        {
            SendResult.Sent => true,
            SendResult.Backpressured => false,
            _ => throw CreateNoWaitSendException(result)
        };
    }

    private static ZlinkSubmitException CreateNoWaitSendException(
        SendResult result)
    {
        return result switch
        {
            SendResult.Backpressured =>
                ZlinkException.CreateSubmitException((int)ErrorCode.EAgain),
            SendResult.NotReady =>
                ZlinkException.CreateSubmitException((int)ErrorCode.ENotConn),
            _ => ZlinkException.CreateSubmitException((int)ErrorCode.EInval)
        };
    }

    private void EnsureSupports(string memberName,
        SocketTypePolicy.SocketCapability capability)
    {
        _policy.EnsureSupportsMember(memberName, capability);
    }

    private void EnsureOptionSupported(SocketOption option)
    {
        _policy.EnsureOptionSupported(option);
    }

}
