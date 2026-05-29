// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    private const int StackSendPartLimit = 8;
    private const int TopicBufferSize = 4096;
    private const int DontWaitFlag = 1;
    private const int ErrnoEAgain = 11;
    private const int ErrnoEWouldBlockWin = 10035;
    private const int ErrnoENotConn = 107;
    private const int ErrnoENotConnWin = 10057;
    private const int ErrnoEHostUnreach = 113;
    private const int ErrnoEHostUnreachWin = 10065;
    private const int ErrnoETimedOut = 110;
    private const int ErrnoETimedOutWin = 10060;

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

    public void Bind(string address)
    {
        BoundaryValidation.ValidateFixedUtf8(address, nameof(address));

        int rc = NativeMethods.zlink_bind(Handle, address);
        if (rc != 0)
            throw ZlinkException.CreateBindException(NativeMethods.zlink_errno());
    }

    public void Connect(string address)
    {
        BoundaryValidation.ValidateFixedUtf8(address, nameof(address));

        int rc = NativeMethods.zlink_connect(Handle, address);
        if (rc != 0)
            throw ZlinkException.CreateConnectException(NativeMethods.zlink_errno());
    }

    public void Unbind(string address)
    {
        BoundaryValidation.ValidateFixedUtf8(address, nameof(address));

        int rc = NativeMethods.zlink_unbind(Handle, address);
        if (rc != 0)
            throw ZlinkException.CreateConnectException(NativeMethods.zlink_errno());
    }

    public void Disconnect(string address)
    {
        BoundaryValidation.ValidateFixedUtf8(address, nameof(address));

        int rc = NativeMethods.zlink_disconnect(Handle, address);
        if (rc != 0)
            throw ZlinkException.CreateConnectException(NativeMethods.zlink_errno());
    }

    public void DisconnectRid(RoutingId peerRid)
    {
        ZlinkRoutingId nativeRid = peerRid.ToNative();
        int rc = NativeMethods.zlink_disconnect_rid(Handle, ref nativeRid);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));

        int rc = NativeMethods.zlink_socket_attach_discovery(Handle,
            discovery.Handle);
        ZlinkException.ThrowConfigIfError(rc);
        _discoveryAttached = true;
    }

    public void AttachStreamRaw(StreamRawPacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamRaw),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new ZlinkHandlerException(HandlerResult.Busy,
                (int)ErrorCode.EBusy);

        SynchronizationContext? context = SynchronizationContext.Current;
        _callbacks.StreamPacketHandler = handler;
        _callbacks.StreamRawContext = context;
        _callbacks.StreamRawNative = OnStreamRaw;
        int rc = NativeMethods.zlink_stream_attach_raw(Handle,
            _callbacks.StreamRawNative, IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.StreamPacketHandler = null;
            _callbacks.StreamRawContext = null;
            _callbacks.StreamRawNative = null;
            throw ZlinkException.CreateHandlerException(
                NativeMethods.zlink_errno());
        }
        _streamAttached = true;
    }

    public void AttachStreamRaw(StreamUInt32PacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamRaw),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new ZlinkHandlerException(HandlerResult.Busy,
                (int)ErrorCode.EBusy);

        SynchronizationContext? context = SynchronizationContext.Current;
        _callbacks.StreamUInt32PacketHandler = handler;
        _callbacks.StreamRawContext = context;
        _callbacks.StreamRawNative = OnStreamRawUInt32;
        int rc = NativeMethods.zlink_stream_attach_raw(Handle,
            _callbacks.StreamRawNative, IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.StreamUInt32PacketHandler = null;
            _callbacks.StreamRawContext = null;
            _callbacks.StreamRawNative = null;
            throw ZlinkException.CreateHandlerException(
                NativeMethods.zlink_errno());
        }
        _streamAttached = true;
    }

    public void AttachStreamPacket(StreamFramedPacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamPacket),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new ZlinkHandlerException(HandlerResult.Busy,
                (int)ErrorCode.EBusy);

        SynchronizationContext? context = SynchronizationContext.Current;
        _callbacks.StreamFramedPacketHandler = handler;
        _callbacks.StreamPacketContext = context;
        _callbacks.StreamPacketNative = OnStreamPacket;
        int rc = NativeMethods.zlink_stream_packet_handler(Handle,
            _callbacks.StreamPacketNative, IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.StreamFramedPacketHandler = null;
            _callbacks.StreamPacketContext = null;
            _callbacks.StreamPacketNative = null;
            throw ZlinkException.CreateHandlerException(
                NativeMethods.zlink_errno());
        }
        _streamAttached = true;
    }

    public void AttachStreamPacket(StreamPacketHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        AttachStreamPacket((routingId, header, body) =>
            handler(ParsePublicRoutingId(routingId), header, body));
    }

    public void AttachStreamPacket(StreamUInt32FramedPacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamPacket),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new ZlinkHandlerException(HandlerResult.Busy,
                (int)ErrorCode.EBusy);

        SynchronizationContext? context = SynchronizationContext.Current;
        _callbacks.StreamUInt32FramedPacketHandler = handler;
        _callbacks.StreamPacketContext = context;
        _callbacks.StreamPacketNative = OnStreamPacketUInt32;
        int rc = NativeMethods.zlink_stream_packet_handler(Handle,
            _callbacks.StreamPacketNative, IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.StreamUInt32FramedPacketHandler = null;
            _callbacks.StreamPacketContext = null;
            _callbacks.StreamPacketNative = null;
            throw ZlinkException.CreateHandlerException(
                NativeMethods.zlink_errno());
        }
        _streamAttached = true;
    }

    public void DetachStream()
    {
        EnsureSupports(nameof(DetachStream),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (!_streamAttached)
            return;

        int rc = NativeMethods.zlink_stream_detach(Handle);
        _streamAttached = false;
        _callbacks.ClearStream();
        ZlinkException.ThrowCloseIfError(rc);
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.MessageSend);
        SendMessageUnchecked(message, flags);
    }

    internal void SendMessageUnchecked(Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        if ((((int)flags) & DontWaitFlag) != 0)
        {
            SendResult result = SendSingleNoWaitResultCore(message);
            if (result != SendResult.Sent)
                throw CreateNoWaitSendException(result);
            return;
        }
        SendSingleCore(message, (int)flags);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal SendResult SendMessageResultUnchecked(Message message, int flags)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        if ((flags & DontWaitFlag) != 0)
            return SendSingleNoWaitResultCore(message);
        return SendSingleResultCore(message, flags);
    }

    public SendResult SendNoWaitResult(Message message)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return SendSingleNoWaitResultCore(message);
    }

    public void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.MessageSend);
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
        {
            SendCore(array.AsSpan(), (int)flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            SendCore(CollectionsMarshal.AsSpan(list), (int)flags, nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        SendCore(copied.AsSpan(), (int)flags, nameof(parts));
    }

    public SendResult SendNoWaitResult(IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return SendPartsNoWaitResult(parts);
    }

    public void Send(string routingId, Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (message == null)
            throw new ArgumentNullException(nameof(message));

        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        if ((((int)flags) & DontWaitFlag) != 0)
        {
            SendResult result = SendSingleNoWaitResultCore(ref nativeRoutingId,
                message);
            if (result != SendResult.Sent)
                throw CreateNoWaitSendException(result);
            return;
        }
        SendSingleCore(ref nativeRoutingId, message, (int)flags);
    }

    public void Send(RoutingId routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        SendRoutedMessageUnchecked(routingId, message, flags);
    }

    internal void SendRoutedMessageUnchecked(RoutingId routingId,
        Message message, SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        ZlinkRoutingId fallback = default;
        ref ZlinkRoutingId nativeRoutingId =
            ref routingId.ToNativeRef(ref fallback);
        if ((((int)flags) & DontWaitFlag) != 0)
        {
            SendResult result = SendSingleNoWaitResultCore(ref nativeRoutingId,
                message);
            if (result != SendResult.Sent)
                throw CreateNoWaitSendException(result);
            return;
        }
        SendSingleCore(ref nativeRoutingId, message, (int)flags);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal SendResult SendRoutedMessageResultUnchecked(RoutingId routingId,
        Message message, int flags)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        // Take ref to the cached ZlinkRoutingId when available — avoids the
        // 256-byte struct copy that ToNative() does in the routed send hot
        // path (51 MB/s memcpy bandwidth at 200K msg/sec).
        ZlinkRoutingId fallback = default;
        ref ZlinkRoutingId nativeRoutingId =
            ref routingId.ToNativeRef(ref fallback);
        return SendSingleResultCore(ref nativeRoutingId, message, flags);
    }

    public void Send(uint routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        ZlinkRoutingId nativeRoutingId = RoutingIdCodec.ToNative(routingId);
        if ((((int)flags) & DontWaitFlag) != 0)
        {
            SendResult result = SendSingleNoWaitResultCore(ref nativeRoutingId,
                message);
            if (result != SendResult.Sent)
                throw CreateNoWaitSendException(result);
            return;
        }
        SendSingleCore(ref nativeRoutingId, message, (int)flags);
    }

    public SendResult SendNoWaitResult(string routingId, Message message)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return SendRoutedSingleNoWaitResult(routingId, message);
    }

    public SendResult SendNoWaitResult(RoutingId routingId, Message message)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        ZlinkRoutingId fallback = default;
        ref ZlinkRoutingId nativeRoutingId =
            ref routingId.ToNativeRef(ref fallback);
        return SendSingleNoWaitResultCore(ref nativeRoutingId, message);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);

        if (parts is Message[] array)
        {
            SendCore(ref nativeRoutingId, array.AsSpan(), (int)flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            SendCore(ref nativeRoutingId, CollectionsMarshal.AsSpan(list), (int)flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        SendCore(ref nativeRoutingId, copied.AsSpan(), (int)flags, nameof(parts));
    }

    public void Send(RoutingId routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        ZlinkRoutingId fallback = default;
        ref ZlinkRoutingId nativeRoutingId =
            ref routingId.ToNativeRef(ref fallback);

        if (parts is Message[] array)
        {
            SendCore(ref nativeRoutingId, array.AsSpan(), (int)flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            SendCore(ref nativeRoutingId, CollectionsMarshal.AsSpan(list), (int)flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        SendCore(ref nativeRoutingId, copied.AsSpan(), (int)flags, nameof(parts));
    }

    public SendResult SendNoWaitResult(string routingId, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return SendRoutedPartsNoWaitResult(routingId, parts);
    }

    public SendResult SendNoWaitResult(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        ZlinkRoutingId fallback = default;
        ref ZlinkRoutingId nativeRoutingId =
            ref routingId.ToNativeRef(ref fallback);

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

    public void Publish(string topic, Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Publish), SocketTypePolicy.SocketCapability.Publish);
        byte[] topicUtf8 = GetValidatedPublishTopicUtf8(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        PublishSingleCore(topicUtf8, message, (int)flags);
    }

    internal SendResult PublishNoWaitResult(string topic, Message message)
    {
        EnsureSupports(nameof(PublishNoWaitResult),
            SocketTypePolicy.SocketCapability.Publish);
        byte[] topicUtf8 = GetValidatedPublishTopicUtf8(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return PublishNoWaitSingleCore(topicUtf8, message);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Publish), SocketTypePolicy.SocketCapability.Publish);
        BoundaryValidation.ValidateTopicOrFilterUtf8(topic, nameof(topic));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), (int)flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), (int)flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), (int)flags, nameof(parts));
    }

    internal SendResult PublishNoWaitResult(string topic, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(PublishNoWaitResult),
            SocketTypePolicy.SocketCapability.Publish);
        BoundaryValidation.ValidateTopicOrFilterUtf8(topic, nameof(topic));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return PublishNoWaitParts(topic, parts);
    }

    public void SetSubscription(string topicOrPattern)
    {
        EnsureSupports(nameof(SetSubscription),
            SocketTypePolicy.SocketCapability.SubscriptionControl);
        BoundaryValidation.ValidateTopicOrFilterUtf8(topicOrPattern,
            nameof(topicOrPattern));

        int rc = NativeMethods.zlink_set_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        EnsureSupports(nameof(UnsetSubscription),
            SocketTypePolicy.SocketCapability.SubscriptionControl);
        BoundaryValidation.ValidateTopicOrFilterUtf8(topicOrPattern,
            nameof(topicOrPattern));

        int rc = NativeMethods.zlink_unset_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public unsafe void RecvHandler(SocketRecvHandler handler)
    {
        EnsureSupports(nameof(RecvHandler),
            SocketTypePolicy.SocketCapability.ReceiveHandler);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        SynchronizationContext? context = SynchronizationContext.Current;
        var socketNative = new NativeMethods.ZlinkSocketMsgHandlerDelegate(
            OnNativeReceive);
        int rc = NativeMethods.zlink_recv_handler(Handle, socketNative, IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.RecvHandler = null;
            _callbacks.RecvHandlerContext = null;
            _callbacks.RecvHandlerNative = null;
            ZlinkException.ThrowHandlerIfError(rc);
        }
        _callbacks.RecvHandler = handler;
        _callbacks.RecvHandlerContext = context;
        _callbacks.RecvHandlerNative = socketNative;
    }

    public void SendReadyHandler(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(Handle, native,
            IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.SendReadyHandler = null;
            _callbacks.SendReadyHandlerContext = null;
            _callbacks.SendReadyHandlerNative = null;
            ZlinkException.ThrowHandlerIfError(rc);
        }
        _callbacks.SendReadyHandler = handler;
        _callbacks.SendReadyHandlerContext = context;
        _callbacks.SendReadyHandlerNative = native;
    }

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
            && ZlinkException.MapErrorCode(ex.InternalErrno) is ErrorCode.EAgain
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

    public unsafe void SubscribeHandler(SocketSubscribeHandler handler)
    {
        EnsureSupports(nameof(SubscribeHandler),
            SocketTypePolicy.SocketCapability.SubscribeHandler);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSubscribeHandlerDelegate(
            OnNativeSubscribe);
        int rc = NativeMethods.zlink_subscribe_handler(Handle, native,
            IntPtr.Zero);
        if (rc != 0)
        {
            _callbacks.SubscribeHandler = null;
            _callbacks.SubscribeHandlerContext = null;
            _callbacks.SubscribeHandlerNative = null;
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
        }
        _callbacks.SubscribeHandler = handler;
        _callbacks.SubscribeHandlerContext = context;
        _callbacks.SubscribeHandlerNative = native;
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


    public void SetOption(SocketOptionKey<int> option, int value)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        _options.SetInt32(option.Option, value);
    }

    public void SetOption(SocketOptionKey<long> option, long value)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        _options.SetInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        _options.SetUInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        EnsureOptionSupported(option.Option);
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(option, value.AsSpan());
    }

    public void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        _options.SetBytes(option.Option, value);
    }

    public void SetOption(SocketOptionKey<string> option, string value)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        _options.SetString(option.Option, value);
    }

    public int GetOption(SocketOptionKey<int> option)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        return _options.GetInt32(option.Option);
    }

    public long GetOption(SocketOptionKey<long> option)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        return _options.GetInt64(option.Option);
    }

    public ulong GetOption(SocketOptionKey<ulong> option)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        return _options.GetUInt64(option.Option);
    }

    public byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        return _options.GetBytes(option.Option, initialSize);
    }

    public int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        return _options.GetBytesInto(option.Option, destination);
    }

    public string GetOption(SocketOptionKey<string> option, int initialSize = 256)
    {
        EnsureOptionSupported(option.Option);
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        return _options.GetString(option.Option, initialSize);
    }

    public SocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All)
    {
        ZlinkSocketMonitorOpenOptions options = new()
        {
            Events = (uint)events
        };
        IntPtr handle = NativeMethods.zlink_socket_monitor_open(Handle, in options);
        if (handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
        return new SocketMonitor(handle);
    }

    public void Dispose()
    {
        Dispose(closeNativeSocket: !_discoveryAttached);
    }

    public void Close()
    {
        Dispose(closeNativeSocket: true);
    }

    private void Dispose(bool closeNativeSocket)
    {
        if (_streamAttached)
        {
            try
            {
                NativeMethods.zlink_stream_detach(Handle);
            }
            catch
            {
            }

            _streamAttached = false;
            _callbacks.ClearStream();
        }

        if (closeNativeSocket)
            _handle.Dispose();
        else
            _handle.ReleaseWithoutClose();
        _callbacks.ClearAllNonStream();
        GC.SuppressFinalize(this);
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

                Message[] copied = new Message[replyParts.Count];
                for (int i = 0; i < copied.Length; i++)
                    copied[i] = replyParts[i];
                SendReplyCore(replyRoutingId.Value, replySpotRid, requestSeq,
                    copied, sendFlags);
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

                Message[] copied = new Message[replyParts.Count];
                for (int i = 0; i < copied.Length; i++)
                    copied[i] = replyParts[i];
                SendReplyCore(replyRoutingId.Value, replySpotRid, requestSeq,
                    copied, sendFlags);
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
        ErrorCode code = ZlinkException.MapErrorCode(ex.InternalErrno);
        return code == ErrorCode.EAgain;
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

    private static SendResult? TryMapSendResultFromErrno()
    {
        return TryMapSendResultFromErrno(NativeMethods.zlink_errno());
    }

    private static SendResult? TryMapSendResultFromErrno(int errno)
    {
        return errno switch
        {
            ErrnoEAgain => SendResult.Backpressured,
            ErrnoEWouldBlockWin => SendResult.Backpressured,
            ErrnoENotConn => SendResult.NotReady,
            ErrnoENotConnWin => SendResult.NotReady,
            ErrnoEHostUnreach => SendResult.NotReady,
            ErrnoEHostUnreachWin => SendResult.NotReady,
            ErrnoETimedOut => SendResult.NotReady,
            ErrnoETimedOutWin => SendResult.NotReady,
            _ => null
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

    private static void CloseStreamPacket(IntPtr msg)
    {
        if (msg == IntPtr.Zero)
            return;
        try
        {
            NativeMethods.zlink_msg_close(msg);
        }
        catch
        {
        }
    }

    private unsafe int OnStreamRaw(IntPtr routingId, IntPtr message, IntPtr userdata)
    {
        if (message == IntPtr.Zero)
            return 0;

        StreamRawPacketHandler? packetHandler = _callbacks.StreamPacketHandler;
        SynchronizationContext? context = _callbacks.StreamRawContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(message);
            return 0;
        }

        ZlinkRoutingId* rid = (ZlinkRoutingId*)routingId;
        Message? payloadMsg = null;
        bool delivered = false;
        try
        {
            payloadMsg = Message.MoveFromNativeSingle(message);
            string routingIdText = RoutingIdCodec.ToPublicString(
                NativeHelpers.ReadRoutingId(ref *rid));
            delivered = true;
            if (context == null)
                return packetHandler(routingIdText, payloadMsg);
            return CallbackDelivery.Invoke(context,
                () => packetHandler(routingIdText, payloadMsg));
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
            if (!delivered && payloadMsg != null)
            {
                try
                {
                    payloadMsg.Dispose();
                }
                catch
                {
                }
            }
            return 1;
        }
        finally
        {
            CloseStreamPacket(message);
        }
    }

    private unsafe int OnStreamRawUInt32(IntPtr routingId, IntPtr message,
        IntPtr userdata)
    {
        if (message == IntPtr.Zero)
            return 0;

        StreamUInt32PacketHandler? packetHandler = _callbacks.StreamUInt32PacketHandler;
        SynchronizationContext? context = _callbacks.StreamRawContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(message);
            return 0;
        }

        byte[] ridBytes = NativeHelpers.ReadRoutingId(
            ref *(ZlinkRoutingId*)routingId);
        if (!RoutingIdCodec.TryToUInt32(ridBytes, out uint routingIdValue))
        {
            CloseStreamPacket(message);
            return 0;
        }

        Message? payloadMsg = null;
        bool delivered = false;
        try
        {
            payloadMsg = Message.MoveFromNativeSingle(message);
            delivered = true;
            if (context == null)
                return packetHandler(routingIdValue, payloadMsg);
            return CallbackDelivery.Invoke(context,
                () => packetHandler(routingIdValue, payloadMsg));
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
            if (!delivered && payloadMsg != null)
            {
                try
                {
                    payloadMsg.Dispose();
                }
                catch
                {
                }
            }
            return 1;
        }
        finally
        {
            CloseStreamPacket(message);
        }
    }

    private unsafe void OnStreamPacket(IntPtr stream, IntPtr routingId,
        IntPtr header, IntPtr body, IntPtr userdata)
    {
        StreamFramedPacketHandler? packetHandler = _callbacks.StreamFramedPacketHandler;
        SynchronizationContext? context = _callbacks.StreamPacketContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
            return;
        }

        Message? headerMsg = null;
        Message? bodyMsg = null;
        bool delivered = false;
        try
        {
            string routingIdText = RoutingIdCodec.ToPublicString(
                NativeHelpers.ReadRoutingId(ref *(ZlinkRoutingId*)routingId));
            headerMsg = Message.MoveFromNativeSingle(header);
            bodyMsg = Message.MoveFromNativeSingle(body);
            delivered = true;
            if (context == null)
                packetHandler(routingIdText, headerMsg, bodyMsg);
            else
                CallbackDelivery.Post(context,
                    () => packetHandler(routingIdText, headerMsg, bodyMsg));
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
            if (!delivered)
            {
                try
                {
                    headerMsg?.Dispose();
                }
                catch
                {
                }

                try
                {
                    bodyMsg?.Dispose();
                }
                catch
                {
                }
            }
        }
        finally
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
        }
    }

    private unsafe void OnStreamPacketUInt32(IntPtr stream, IntPtr routingId,
        IntPtr header, IntPtr body, IntPtr userdata)
    {
        StreamUInt32FramedPacketHandler? packetHandler =
            _callbacks.StreamUInt32FramedPacketHandler;
        SynchronizationContext? context = _callbacks.StreamPacketContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
            return;
        }

        byte[] ridBytes = NativeHelpers.ReadRoutingId(
            ref *(ZlinkRoutingId*)routingId);
        if (!RoutingIdCodec.TryToUInt32(ridBytes, out uint routingIdValue))
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
            return;
        }

        Message? headerMsg = null;
        Message? bodyMsg = null;
        bool delivered = false;
        try
        {
            headerMsg = Message.MoveFromNativeSingle(header);
            bodyMsg = Message.MoveFromNativeSingle(body);
            delivered = true;
            if (context == null)
                packetHandler(routingIdValue, headerMsg, bodyMsg);
            else
                CallbackDelivery.Post(context,
                    () => packetHandler(routingIdValue, headerMsg, bodyMsg));
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
            if (!delivered)
            {
                try
                {
                    headerMsg?.Dispose();
                }
                catch
                {
                }

                try
                {
                    bodyMsg?.Dispose();
                }
                catch
                {
                }
            }
        }
        finally
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
        }
    }

    private unsafe void OnNativeSubscribe(IntPtr sourceRoutingId, byte* topic,
        nuint topicLen, IntPtr parts, nuint partCount, IntPtr userData)
    {
        SocketSubscribeHandler? handler = _callbacks.SubscribeHandler;
        SynchronizationContext? context = _callbacks.SubscribeHandlerContext;
        if (handler == null)
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            return;
        }

        Message[]? managedParts = null;
        bool delivered = false;
        try
        {
            string routingId = string.Empty;
            if (sourceRoutingId != IntPtr.Zero)
            {
                ZlinkRoutingId* nativeRoutingId = (ZlinkRoutingId*)sourceRoutingId;
                routingId = RoutingIdCodec.ToPublicString(
                    NativeHelpers.ReadRoutingId(ref *nativeRoutingId));
            }

            string topicId = topic == null || topicLen == 0
                ? string.Empty
                : Encoding.UTF8.GetString(
                    new ReadOnlySpan<byte>(topic, checked((int)topicLen)));
            managedParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            delivered = true;
            CallbackDelivery.Post(context,
                () => handler(routingId, topicId, managedParts));
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
            if (!delivered && managedParts != null)
            {
                foreach (Message part in managedParts)
                    part.Dispose();
            }
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
        }
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _callbacks.SendReadyHandler;
        SynchronizationContext? context = _callbacks.SendReadyHandlerContext;
        if (handler == null)
            return;

        try
        {
            CallbackDelivery.Post(context, handler);
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
        }
    }

    private unsafe void OnNativeReceive(IntPtr sourceRoutingId, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        SocketRecvHandler? handler = _callbacks.RecvHandler;
        SynchronizationContext? context = _callbacks.RecvHandlerContext;
        if (handler == null)
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            return;
        }

        Message[]? managedParts = null;
        bool delivered = false;
        try
        {
            string routingId = string.Empty;
            if (sourceRoutingId != IntPtr.Zero)
            {
                ZlinkRoutingId* nativeRoutingId = (ZlinkRoutingId*)sourceRoutingId;
                routingId = RoutingIdCodec.ToPublicString(
                    NativeHelpers.ReadRoutingId(ref *nativeRoutingId));
            }

            managedParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            delivered = true;
            CallbackDelivery.Post(context,
                () => handler(routingId, managedParts));
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
            if (!delivered && managedParts != null)
            {
                foreach (Message part in managedParts)
                    part.Dispose();
            }
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
        }
    }

    private unsafe void SendSingleCore(Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return;
            }
            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            throw ZlinkException.CreateSubmitException(errno);
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private unsafe SendResult SendSingleResultCore(Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            SendResult? mappedResult = TryMapSendResultFromErrno(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private unsafe SendResult SendSingleNoWaitResultCore(Message message)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            // DONT_WAIT-only critical variant: contractually non-blocking.
            int rc = NativeMethods.zlink_send_part_nowait(Handle, ref nativePart,
                DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            SendResult? mappedResult = TryMapSendResultFromErrno(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishSingleCore(string topic, Message message,
        int flags)
    {
        PublishSingleCore(GetPublishTopicUtf8(topic), message, flags);
    }

    private unsafe void PublishSingleCore(byte[] topicUtf8, Message message,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            fixed (byte* topicPtr = topicUtf8)
            {
                int rc = NativeMethods.zlink_publish_part_utf8(Handle,
                    topicPtr, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final);
                if (rc == 0)
                {
                    shouldRestore = false;
                    return;
                }
                int errno = NativeMethods.zlink_errno();
                message.RestoreFrom(ref nativePart);
                shouldRestore = false;
                throw ZlinkException.CreateSubmitException(errno);
            }
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult PublishNoWaitSingleCore(string topic, Message message)
    {
        return PublishNoWaitSingleCore(GetPublishTopicUtf8(topic), message);
    }

    private unsafe SendResult PublishNoWaitSingleCore(byte[] topicUtf8,
        Message message)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            fixed (byte* topicPtr = topicUtf8)
            {
                int rc = NativeMethods.zlink_publish_part_utf8(Handle,
                    topicPtr, ref nativePart, DontWaitFlag,
                    NativeMethods.ZlinkPartFlag.Final);
                if (rc == 0)
                {
                    shouldRestore = false;
                    return SendResult.Sent;
                }
            }

            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            SendResult? sendResult = TryMapSendResultFromErrno(errno);
            if (sendResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return sendResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private byte[] GetPublishTopicUtf8(string topic)
    {
        byte[]? cached = _publishTopicCacheUtf8;
        string? cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
        {
            return cached;
        }

        byte[] encoded = PublishTopicEncoding.GetNullTerminatedUtf8(topic);
        _publishTopicCacheKey = topic;
        _publishTopicCacheUtf8 = encoded;
        return encoded;
    }

    private byte[] GetValidatedPublishTopicUtf8(string topic, string paramName)
    {
        byte[]? cached = _publishTopicCacheUtf8;
        string? cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
        {
            return cached;
        }

        BoundaryValidation.ValidateTopicOrFilterUtf8(topic, paramName);
        byte[] encoded = PublishTopicEncoding.GetNullTerminatedUtf8(topic);
        _publishTopicCacheKey = topic;
        _publishTopicCacheUtf8 = encoded;
        return encoded;
    }

    private unsafe void SendSingleCore(ref ZlinkRoutingId routingId,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            int rc = (flags & DontWaitFlag) != 0
                ? NativeMethods.zlink_send_part_rid_nowait(Handle,
                    ref routingId, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final)
                : NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                    ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return;
            }
            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            throw ZlinkException.CreateSubmitException(errno);
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private unsafe SendResult SendSingleResultCore(ref ZlinkRoutingId routingId,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            int rc = (flags & DontWaitFlag) != 0
                ? NativeMethods.zlink_send_part_rid_nowait(Handle,
                    ref routingId, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final)
                : NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                    ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            SendResult? mappedResult = TryMapSendResultFromErrno(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult SendSingleNoWaitResultCore(ref ZlinkRoutingId routingId,
        Message message)
    {
        ZlinkMsg nativePart = default;
        bool shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            int rc = NativeMethods.zlink_send_part_rid_nowait(Handle,
                ref routingId, ref nativePart, DontWaitFlag,
                NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            int errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            SendResult? mappedResult = TryMapSendResultFromErrno(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
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
