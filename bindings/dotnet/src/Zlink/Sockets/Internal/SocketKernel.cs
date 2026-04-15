// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Zlink.Native;

namespace Zlink.Sockets.Internal;

internal sealed class SocketKernel : IDisposable
{
    private static readonly NativeMethods.ZlinkFreeFnDelegate BorrowedBufferFree =
        OnBorrowedBufferFree;
    private static readonly IntPtr BorrowedBufferFreePtr =
        Marshal.GetFunctionPointerForDelegate(BorrowedBufferFree);
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
    private NativeMethods.ZlinkStreamOnRawDelegate? _streamRawCallback;
    private StreamPacketHandler? _streamPacketHandler;
    private SocketRecvHandler? _recvHandler;
    private SocketSubscribeHandler? _subscribeHandler;
    private Action? _sendReadyHandler;
    private SynchronizationContext? _streamRawContext;
    private SynchronizationContext? _recvHandlerContext;
    private SynchronizationContext? _subscribeHandlerContext;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSocketMsgHandlerDelegate? _recvHandlerNative;
    private NativeMethods.ZlinkSubscribeHandlerDelegate? _subscribeHandlerNative;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private bool _streamAttached;

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
        if (address == null)
            throw new ArgumentNullException(nameof(address));

        int rc = NativeMethods.zlink_bind(Handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Connect(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));

        int rc = NativeMethods.zlink_connect(Handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unbind(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));

        int rc = NativeMethods.zlink_unbind(Handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Disconnect(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));

        int rc = NativeMethods.zlink_disconnect(Handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));

        int rc = NativeMethods.zlink_socket_attach_discovery(Handle,
            discovery.Handle);
        ZlinkException.ThrowIfError(rc);
    }

    public void AttachStreamRaw(StreamPacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamRaw),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        SynchronizationContext? context = SynchronizationContext.Current;
        _streamPacketHandler = handler;
        _streamRawContext = context;
        _streamRawCallback = OnStreamRaw;
        int rc = NativeMethods.zlink_stream_attach_raw(Handle,
            _streamRawCallback, IntPtr.Zero);
        if (rc != 0)
        {
            _streamPacketHandler = null;
            _streamRawContext = null;
            _streamRawCallback = null;
            throw ZlinkException.FromLastError();
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
        _streamPacketHandler = null;
        _streamRawCallback = null;
        _streamRawContext = null;
        ZlinkException.ThrowIfError(rc);
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.MessageSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        SendSingleCore(message, (int)flags);
    }

    public SendResult TrySend(Message message)
    {
        EnsureSupports(nameof(TrySend),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return TrySendSingleCore(message);
    }

    internal void SendRawSingle(ReadOnlySpan<byte> payload, int flags)
    {
        EnsureSupports(nameof(SendRawSingle),
            SocketTypePolicy.SocketCapability.MessageSend);
        SendRawSingleCore(payload, flags);
    }

    internal SendResult TrySendRawSingle(ReadOnlySpan<byte> payload)
    {
        EnsureSupports(nameof(TrySendRawSingle),
            SocketTypePolicy.SocketCapability.MessageSend);
        return TrySendRawSingleCore(payload);
    }

    internal void SendBorrowedSingle(byte[] payload, int flags)
    {
        EnsureSupports(nameof(SendBorrowedSingle),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        SendBorrowedSingleCore(payload, flags);
    }

    internal SendResult TrySendBorrowedSingle(byte[] payload)
    {
        EnsureSupports(nameof(TrySendBorrowedSingle),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        return TrySendBorrowedSingleCore(payload);
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

    public SendResult TrySend(IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(TrySend),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return TrySendPartsWithFlags(parts);
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
        SendSingleCore(ref nativeRoutingId, message, (int)flags);
    }

    public void Send(RoutingId routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendSingleCore(ref nativeRoutingId, message, (int)flags);
    }

    public SendResult TrySend(string routingId, Message message)
    {
        EnsureSupports(nameof(TrySend),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return TrySendRoutedSingleWithFlags(routingId, message);
    }

    public SendResult TrySend(RoutingId routingId, Message message)
    {
        EnsureSupports(nameof(TrySend),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return TrySendSingleCore(ref nativeRoutingId, message);
    }

    internal void SendBorrowedSingle(string routingId, byte[] payload,
        int flags)
    {
        EnsureSupports(nameof(SendBorrowedSingle),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendBorrowedSingleCore(ref nativeRoutingId, payload, flags);
    }

    internal void SendBorrowedSingle(RoutingId routingId, byte[] payload,
        int flags)
    {
        EnsureSupports(nameof(SendBorrowedSingle),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendBorrowedSingleCore(ref nativeRoutingId, payload, flags);
    }

    internal SendResult TrySendBorrowedSingle(string routingId, byte[] payload)
    {
        EnsureSupports(nameof(TrySendBorrowedSingle),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return TrySendBorrowedSingleCore(ref nativeRoutingId, payload);
    }

    internal SendResult TrySendBorrowedSingle(RoutingId routingId, byte[] payload)
    {
        EnsureSupports(nameof(TrySendBorrowedSingle),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return TrySendBorrowedSingleCore(ref nativeRoutingId, payload);
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

        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
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

    public SendResult TrySend(string routingId, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(TrySend),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return TrySendRoutedPartsWithFlags(routingId, parts);
    }

    public SendResult TrySend(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(TrySend),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);

        if (parts is Message[] array)
            return TrySendCore(ref nativeRoutingId, array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
        {
            return TrySendCore(ref nativeRoutingId,
                CollectionsMarshal.AsSpan(list), nameof(parts));
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TrySendCore(ref nativeRoutingId, copied.AsSpan(), nameof(parts));
    }

    public void Publish(string topic, Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Publish), SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        PublishSingleCore(topic, message, (int)flags);
    }

    public SendResult TryPublish(string topic, Message message)
    {
        EnsureSupports(nameof(TryPublish),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return TryPublishSingleCore(topic, message);
    }

    internal void PublishRawSingle(string topic, ReadOnlySpan<byte> payload,
        int flags)
    {
        EnsureSupports(nameof(PublishRawSingle),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        PublishRawSingleCore(topic, payload, flags);
    }

    internal SendResult TryPublishRawSingle(string topic,
        ReadOnlySpan<byte> payload)
    {
        EnsureSupports(nameof(TryPublishRawSingle),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        return TryPublishRawSingleCore(topic, payload);
    }

    internal void PublishBorrowedSingle(string topic, byte[] payload, int flags)
    {
        EnsureSupports(nameof(PublishBorrowedSingle),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        PublishBorrowedSingleCore(topic, payload, flags);
    }

    internal SendResult TryPublishBorrowedSingle(string topic, byte[] payload)
    {
        EnsureSupports(nameof(TryPublishBorrowedSingle),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        return TryPublishBorrowedSingleCore(topic, payload);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Publish), SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
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

    public SendResult TryPublish(string topic, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(TryPublish),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return TryPublishPartsWithFlags(topic, parts);
    }

    public void SetSubscription(string topicOrPattern)
    {
        EnsureSupports(nameof(SetSubscription),
            SocketTypePolicy.SocketCapability.SubscriptionControl);
        if (topicOrPattern == null)
            throw new ArgumentNullException(nameof(topicOrPattern));

        int rc = NativeMethods.zlink_set_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        EnsureSupports(nameof(UnsetSubscription),
            SocketTypePolicy.SocketCapability.SubscriptionControl);
        if (topicOrPattern == null)
            throw new ArgumentNullException(nameof(topicOrPattern));

        int rc = NativeMethods.zlink_unset_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowIfError(rc);
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
            _recvHandler = null;
            _recvHandlerContext = null;
            _recvHandlerNative = null;
            ZlinkException.ThrowIfError(rc);
        }
        _recvHandler = handler;
        _recvHandlerContext = context;
        _recvHandlerNative = socketNative;
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
            _sendReadyHandler = null;
            _sendReadyHandlerContext = null;
            _sendReadyHandlerNative = null;
            ZlinkException.ThrowIfError(rc);
        }
        _sendReadyHandler = handler;
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
    }

    public TopicMessage Subscribe(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(Subscribe),
            SocketTypePolicy.SocketCapability.SubscribeReceive);
        return SubscribeCore((int)flags);
    }

    public TopicMessage? TrySubscribe()
    {
        EnsureSupports(nameof(Subscribe),
            SocketTypePolicy.SocketCapability.SubscribeReceive);
        return TryReceiveCore(() => SubscribeCore(DontWaitFlag));
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

    public unsafe SubscriptionEvent ReceiveSubscriptionEvent(
        RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(ReceiveSubscriptionEvent),
            SocketTypePolicy.SocketCapability.SubscriptionEvents);
        return ReceiveSubscriptionEventCore((int)flags);
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
            _subscribeHandler = null;
            _subscribeHandlerContext = null;
            _subscribeHandlerNative = null;
            ZlinkException.ThrowIfError(rc);
        }
        _subscribeHandler = handler;
        _subscribeHandlerContext = context;
        _subscribeHandlerNative = native;
    }

    public SubscriptionEvent? TryReceiveSubscriptionEvent()
    {
        EnsureSupports(nameof(ReceiveSubscriptionEvent),
            SocketTypePolicy.SocketCapability.SubscriptionEvents);
        return TryReceiveCore(() => ReceiveSubscriptionEventCore(DontWaitFlag));
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(Recv),
            SocketTypePolicy.SocketCapability.MessageReceive);
        return ReceiveCore((int)flags);
    }

    public Received? TryRecv()
    {
        EnsureSupports(nameof(TryRecv),
            SocketTypePolicy.SocketCapability.MessageReceive);
        return TryReceiveCore(() => ReceiveCore(DontWaitFlag));
    }

    public Received ReceiveRouted(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(ReceiveRouted),
            SocketTypePolicy.SocketCapability.RoutedReceive);
        return ReceiveRoutedCore((int)flags);
    }

    public Received? TryReceiveRouted()
    {
        EnsureSupports(nameof(TryReceiveRouted),
            SocketTypePolicy.SocketCapability.RoutedReceive);
        return TryReceiveCore(() => ReceiveRoutedCore(DontWaitFlag));
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
        EnsureSupports(nameof(TryReceiveRawRoutedFrame),
            SocketTypePolicy.SocketCapability.RoutedReceive);
        try
        {
            return ReceiveRawRoutedFrameCore(routingDestination,
                payloadDestination, flags, out pendingFrames);
        }
        catch (ZlinkException ex) when (MapTryReceiveableError(ex))
        {
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
            throw ZlinkException.FromLastError();
        return new SocketMonitor(handle);
    }

    public void Dispose()
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
            _streamPacketHandler = null;
            _streamRawCallback = null;
            _streamRawContext = null;
        }

        _handle.Dispose();
        _recvHandler = null;
        _recvHandlerContext = null;
        _subscribeHandler = null;
        _subscribeHandlerContext = null;
        _sendReadyHandler = null;
        _sendReadyHandlerContext = null;
        _recvHandlerNative = null;
        _subscribeHandlerNative = null;
        _sendReadyHandlerNative = null;
        GC.SuppressFinalize(this);
    }

    private unsafe void SendReplyCore(RoutingId routingId,
        RoutingId? spotRid, ulong requestSequence, IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        _ = flags;
        byte[] ridBytes = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
        byte[]? spotRidBytes = null;
        ZlinkRoutingId nativeSpotRid = default;
        if (spotRid.HasValue)
        {
            spotRidBytes = RoutingIdCodec.FromRoutingId(spotRid.Value);
            nativeSpotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        }
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = spotRidBytes == null
                    ? NativeMethods.zlink_router_reply(Handle,
                        ref nativeRoutingId, requestSequence, (IntPtr)nativePtr,
                        (nuint)nativeParts.Length)
                    : NativeMethods.zlink_router_reply_spot(Handle,
                        ref nativeRoutingId, ref nativeSpotRid, requestSequence,
                        (IntPtr)nativePtr, (nuint)nativeParts.Length);
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
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

    private SendResult TrySendPartsWithFlags(IReadOnlyList<Message> parts)
    {
        if (parts is Message[] array)
            return TrySendCore(array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
            return TrySendCore(CollectionsMarshal.AsSpan(list), nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TrySendCore(copied.AsSpan(), nameof(parts));
    }

    private void SendRoutedSingleWithFlags(string routingId, Message message,
        int flags)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendSingleCore(ref nativeRoutingId, message, flags);
    }

    private SendResult TrySendRoutedSingleWithFlags(string routingId,
        Message message)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return TrySendSingleCore(ref nativeRoutingId, message);
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

    private SendResult TrySendRoutedPartsWithFlags(string routingId,
        IReadOnlyList<Message> parts)
    {
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);

        if (parts is Message[] array)
            return TrySendCore(ref nativeRoutingId, array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
        {
            return TrySendCore(ref nativeRoutingId,
                CollectionsMarshal.AsSpan(list), nameof(parts));
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TrySendCore(ref nativeRoutingId, copied.AsSpan(), nameof(parts));
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

    private SendResult TryPublishPartsWithFlags(string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts is Message[] array)
            return TryPublishCore(topic, array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
            return TryPublishCore(topic, CollectionsMarshal.AsSpan(list),
                nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TryPublishCore(topic, copied.AsSpan(), nameof(parts));
    }

    private unsafe TopicMessage SubscribeCore(int flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_subscribe(Handle,
                (IntPtr)(&nativeRoutingId), out nativeParts, out partCount,
                topicBuffer, ref topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            RoutingId? routingId = RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            string topic = DecodeTopic(topicBuffer, topicLength);
            Message[] parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
            if (parts.Length == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return new TopicMessage(routingId, null, topic, parts);
        }
        catch
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            throw;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe byte[][] ReceiveRawSubscribedFramesCore(int flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            int rc = NativeMethods.zlink_subscribe(Handle, IntPtr.Zero,
                out nativeParts, out partCount, topicBuffer, ref topicLength,
                flags);
            ZlinkException.ThrowIfError(rc);

            if (partCount == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            int total = checked((int)partCount);
            var frames = new byte[total][];
            ZlinkMsg* src = (ZlinkMsg*)nativeParts;
            for (int i = 0; i < total; i++)
            {
                IntPtr msgPtr = new IntPtr(src + i);
                int size = checked((int)NativeMethods.zlink_msg_size(msgPtr));
                if (size == 0)
                {
                    frames[i] = Array.Empty<byte>();
                    continue;
                }

                IntPtr dataPtr = NativeMethods.zlink_msg_data(msgPtr);
                if (dataPtr == IntPtr.Zero)
                {
                    frames[i] = Array.Empty<byte>();
                    continue;
                }

                byte[] payload = new byte[size];
                new ReadOnlySpan<byte>((void*)dataPtr, size).CopyTo(payload);
                frames[i] = payload;
            }

            return frames;
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe int ReceiveRawSubscribedFrameCore(Span<byte> destination,
        int flags, out byte[][] pendingFrames)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            int rc = NativeMethods.zlink_subscribe(Handle, IntPtr.Zero,
                out nativeParts, out partCount, topicBuffer, ref topicLength,
                flags);
            ZlinkException.ThrowIfError(rc);
            return CopyFirstFrameAndCollectPending(nativeParts, partCount,
                destination, out pendingFrames);
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_subscription_event(Handle,
                (IntPtr)(&nativeRoutingId), out int subscribedInt, topicBuffer,
                ref topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            RoutingId? routingId = RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            string topic = DecodeTopic(topicBuffer, topicLength);
            return new SubscriptionEvent(routingId, null, topic,
                subscribedInt != 0);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe Received ReceiveCore(int flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        try
        {
            int rc = NativeMethods.zlink_recv(Handle, IntPtr.Zero, out nativeParts,
                out partCount, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            Message[] parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
            if (parts.Length == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return new Received(null, parts);
        }
        catch
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            throw;
        }
    }

    private unsafe Received ReceiveRoutedCore(int flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        ZlinkRoutingId nativeRoutingId = default;
        ZlinkRoutingId nativeSpotRoutingId = default;
        IntPtr nativeSourceNodeRoutingId = IntPtr.Zero;
        IntPtr nativeSourceSpotRoutingId = IntPtr.Zero;
        ulong requestSequence = 0;
        try
        {
            int rc;
            if (Type == SocketType.Router)
            {
                rc = NativeMethods.zlink_router_recv(Handle,
                    out nativeSourceNodeRoutingId, out nativeSourceSpotRoutingId,
                    out requestSequence,
                    out nativeParts, out partCount, flags);
                if (rc == 0 && nativeSourceNodeRoutingId != IntPtr.Zero)
                    nativeRoutingId = *(ZlinkRoutingId*)nativeSourceNodeRoutingId;
                if (rc == 0 && nativeSourceSpotRoutingId != IntPtr.Zero)
                    nativeSpotRoutingId = *(ZlinkRoutingId*)nativeSourceSpotRoutingId;
            }
            else
            {
                rc = NativeMethods.zlink_recv(Handle, out nativeRoutingId,
                    out nativeParts, out partCount, flags);
            }
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            Message[] parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
            if (parts.Length == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            RoutingId? routingId = RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            RoutingId? spotRid = nativeSpotRoutingId.Size == 0
                ? null
                : RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref nativeSpotRoutingId));
            if (requestSequence == 0)
                return new Received(routingId, parts, spotRid: spotRid);

            return new Received(routingId, parts, requestSequence, spotRid,
                replyHandler: (replyParts, sendFlags) =>
                {
                    if (routingId is null)
                    {
                        throw new ZlinkSubmitException(
                            SubmitResult.InvalidArgument,
                            (int)ErrorCode.EInval);
                    }

                    Message[] copied = new Message[replyParts.Count];
                    for (int i = 0; i < copied.Length; i++)
                        copied[i] = replyParts[i];
                    SendReplyCore(routingId.Value, spotRid, requestSequence, copied,
                        sendFlags);
                });
        }
        catch
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            throw;
        }
    }

    private unsafe byte[][] ReceiveRawFramesCore(int flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        ZlinkRoutingId nativeRoutingId = default;
        ZlinkRoutingId nativeSpotRoutingId = default;
        IntPtr nativeSourceNodeRoutingId = IntPtr.Zero;
        IntPtr nativeSourceSpotRoutingId = IntPtr.Zero;
        ulong requestSequence = 0;
        try
        {
            int rc;
            if (Type == SocketType.Router)
            {
                rc = NativeMethods.zlink_router_recv(Handle,
                    out nativeSourceNodeRoutingId, out nativeSourceSpotRoutingId,
                    out requestSequence,
                    out nativeParts, out partCount, flags);
                if (rc == 0 && nativeSourceNodeRoutingId != IntPtr.Zero)
                    nativeRoutingId = *(ZlinkRoutingId*)nativeSourceNodeRoutingId;
                if (rc == 0 && nativeSourceSpotRoutingId != IntPtr.Zero)
                    nativeSpotRoutingId = *(ZlinkRoutingId*)nativeSourceSpotRoutingId;
            }
            else
            {
                rc = NativeMethods.zlink_recv(Handle, out nativeRoutingId,
                    out nativeParts, out partCount, flags);
            }
            ZlinkException.ThrowIfError(rc);

            int routingCount = 0;
            if (nativeRoutingId.Size > 0)
                routingCount++;
            if (nativeSpotRoutingId.Size > 0)
                routingCount++;
            int total = checked((int)partCount) + routingCount;
            var frames = new byte[total][];
            int index = 0;
            if (nativeRoutingId.Size > 0)
                frames[index++] = NativeHelpers.ReadRoutingId(ref nativeRoutingId);
            if (nativeSpotRoutingId.Size > 0)
                frames[index++] = NativeHelpers.ReadRoutingId(ref nativeSpotRoutingId);

            ZlinkMsg* src = (ZlinkMsg*)nativeParts;
            for (int i = 0; i < (int)partCount; i++)
            {
                IntPtr msgPtr = new IntPtr(src + i);
                int size = checked((int)NativeMethods.zlink_msg_size(msgPtr));
                if (size == 0)
                {
                    frames[index++] = Array.Empty<byte>();
                    continue;
                }

                IntPtr dataPtr = NativeMethods.zlink_msg_data(msgPtr);
                if (dataPtr == IntPtr.Zero)
                {
                    frames[index++] = Array.Empty<byte>();
                    continue;
                }

                byte[] payload = new byte[size];
                new ReadOnlySpan<byte>((void*)dataPtr, size).CopyTo(payload);
                frames[index++] = payload;
            }

            return frames;
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
        }
    }

    private unsafe int ReceiveRawFrameCore(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        try
        {
            int rc = NativeMethods.zlink_recv(Handle, IntPtr.Zero,
                out nativeParts, out partCount, flags);
            ZlinkException.ThrowIfError(rc);
            if (partCount == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return CopyFirstFrameAndCollectPending(nativeParts, partCount,
                destination, out pendingFrames);
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
        }
    }

    private unsafe int ReceiveRawRoutedFrameCore(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out byte[][] pendingFrames)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        ZlinkRoutingId nativeRoutingId = default;
        IntPtr nativeSourceNodeRoutingId = IntPtr.Zero;
        ulong requestSequence = 0;
        try
        {
            int rc;
            if (Type == SocketType.Router)
            {
                rc = NativeMethods.zlink_router_recv(Handle,
                    out nativeSourceNodeRoutingId, out _,
                    out requestSequence,
                    out nativeParts, out partCount, flags);
                if (rc == 0 && nativeSourceNodeRoutingId != IntPtr.Zero)
                    nativeRoutingId = *(ZlinkRoutingId*)nativeSourceNodeRoutingId;
            }
            else
            {
                rc = NativeMethods.zlink_recv(Handle, out nativeRoutingId,
                    out nativeParts, out partCount, flags);
            }
            ZlinkException.ThrowIfError(rc);
            if (partCount == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            _ = requestSequence;
            CopyRoutingId(ref nativeRoutingId, routingDestination);
            return CopyFirstFrameAndCollectPending(nativeParts, partCount,
                payloadDestination, out pendingFrames);
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
        }
    }

    private static unsafe int CopyFirstFrameAndCollectPending(IntPtr nativeParts,
        nuint partCount, Span<byte> destination, out byte[][] pendingFrames)
    {
        int total = checked((int)partCount);
        if (total <= 0)
        {
            pendingFrames = Array.Empty<byte[]>();
            return 0;
        }

        ZlinkMsg* src = (ZlinkMsg*)nativeParts;
        IntPtr firstPtr = new IntPtr(src);
        int firstSize = checked((int)NativeMethods.zlink_msg_size(firstPtr));
        if (firstSize > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        IntPtr firstData = NativeMethods.zlink_msg_data(firstPtr);
        if (firstSize != 0 && firstData != IntPtr.Zero)
            new ReadOnlySpan<byte>((void*)firstData, firstSize).CopyTo(destination);

        if (total == 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return firstSize;
        }

        pendingFrames = new byte[total - 1][];
        for (int i = 1; i < total; i++)
        {
            IntPtr msgPtr = new IntPtr(src + i);
            int size = checked((int)NativeMethods.zlink_msg_size(msgPtr));
            if (size == 0)
            {
                pendingFrames[i - 1] = Array.Empty<byte>();
                continue;
            }

            IntPtr dataPtr = NativeMethods.zlink_msg_data(msgPtr);
            if (dataPtr == IntPtr.Zero)
            {
                pendingFrames[i - 1] = Array.Empty<byte>();
                continue;
            }

            byte[] payload = new byte[size];
            new ReadOnlySpan<byte>((void*)dataPtr, size).CopyTo(payload);
            pendingFrames[i - 1] = payload;
        }

        return firstSize;
    }

    private static unsafe void CopyRoutingId(ref ZlinkRoutingId routingId,
        Span<byte> destination)
    {
        int size = routingId.Size;
        if (size > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        if (size <= 0)
            return;

        fixed (byte* src = routingId.Data)
            new ReadOnlySpan<byte>(src, size).CopyTo(destination);
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

    private static void OnBorrowedBufferFree(IntPtr data, IntPtr hint)
    {
        if (hint == IntPtr.Zero)
            return;
        GCHandle.FromIntPtr(hint).Free();
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
        int errno = NativeMethods.zlink_errno();
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

        StreamPacketHandler? packetHandler = _streamPacketHandler;
        SynchronizationContext? context = _streamRawContext;
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
            return CallbackDelivery.Invoke(context,
                () => packetHandler(routingIdText, payloadMsg));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
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

    private unsafe void OnNativeSubscribe(IntPtr sourceRoutingId, byte* topic,
        nuint topicLen, IntPtr parts, nuint partCount, IntPtr userData)
    {
        SocketSubscribeHandler? handler = _subscribeHandler;
        SynchronizationContext? context = _subscribeHandlerContext;
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
            Runtime.ReportUnhandledCallbackException(ex);
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
        Action? handler = _sendReadyHandler;
        SynchronizationContext? context = _sendReadyHandlerContext;
        if (handler == null)
            return;

        try
        {
            CallbackDelivery.Post(context, handler);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private unsafe void OnNativeReceive(IntPtr sourceRoutingId, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        SocketRecvHandler? handler = _recvHandler;
        SynchronizationContext? context = _recvHandlerContext;
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
            Runtime.ReportUnhandledCallbackException(ex);
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

    private unsafe void SendCore(ReadOnlySpan<Message> parts, int flags,
        string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            int rc;
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                rc = NativeMethods.zlink_send(Handle, (IntPtr)nativePtr,
                    (nuint)parts.Length, flags);
            }
            if (rc < 0)
            {
                RestoreManagedParts(parts, nativeParts, built);
                built = 0;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult TrySendCore(ReadOnlySpan<Message> parts,
        string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            int rc;
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                rc = NativeMethods.zlink_send(Handle, (IntPtr)nativePtr,
                    (nuint)parts.Length, DontWaitFlag);
            }
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                RestoreManagedParts(parts, nativeParts, built);
                built = 0;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void SendSingleCore(Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                flags);
            if (rc != 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult TrySendSingleCore(Message message)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void SendRawSingleCore(ReadOnlySpan<byte> payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult TrySendRawSingleCore(ReadOnlySpan<byte> payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe void SendBorrowedSingleCore(byte[] payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult TrySendBorrowedSingleCore(byte[] payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            int rc;
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                rc = NativeMethods.zlink_publish(Handle, topic, (IntPtr)nativePtr,
                    (nuint)parts.Length, flags);
            }
            if (rc < 0)
            {
                RestoreManagedParts(parts, nativeParts, built);
                built = 0;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult TryPublishCore(string topic,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            int rc;
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                rc = NativeMethods.zlink_publish(Handle, topic,
                    (IntPtr)nativePtr, (nuint)parts.Length, DontWaitFlag);
            }
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                RestoreManagedParts(parts, nativeParts, built);
                built = 0;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void PublishSingleCore(string topic, Message message,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, flags);
            if (rc != 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult TryPublishSingleCore(string topic, Message message)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishRawSingleCore(string topic,
        ReadOnlySpan<byte> payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult TryPublishRawSingleCore(string topic,
        ReadOnlySpan<byte> payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishBorrowedSingleCore(string topic, byte[] payload,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult TryPublishBorrowedSingleCore(string topic,
        byte[] payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe void SendCore(ref ZlinkRoutingId routingId,
        ReadOnlySpan<Message> parts, int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            int rc;
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                    (IntPtr)nativePtr, (nuint)parts.Length, flags);
            }
            if (rc < 0)
            {
                RestoreManagedParts(parts, nativeParts, built);
                built = 0;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult TrySendCore(ref ZlinkRoutingId routingId,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            int rc;
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                    (IntPtr)nativePtr, (nuint)parts.Length, DontWaitFlag);
            }
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                RestoreManagedParts(parts, nativeParts, built);
                built = 0;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void SendSingleCore(ref ZlinkRoutingId routingId,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                (IntPtr)(&nativePart), 1, flags);
            if (rc != 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult TrySendSingleCore(ref ZlinkRoutingId routingId,
        Message message)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void SendBorrowedSingleCore(ref ZlinkRoutingId routingId,
        byte[] payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                (IntPtr)(&nativePart), 1, flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult TrySendBorrowedSingleCore(
        ref ZlinkRoutingId routingId, byte[] payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private static void PrepareNativeParts(ReadOnlySpan<Message> parts,
        Span<ZlinkMsg> nativeParts, string paramName, ref int built)
    {
        for (int i = 0; i < parts.Length; i++)
        {
            if (parts[i] == null)
            {
                throw new ArgumentException(
                    "Parts must not contain null messages.", paramName);
            }
        }

        for (int i = 0; i < parts.Length; i++)
        {
            parts[i].MoveTo(ref nativeParts[i]);
            built++;
        }
    }

    private static void RestoreManagedParts(ReadOnlySpan<Message> parts,
        Span<ZlinkMsg> nativeParts, int count)
    {
        for (int i = 0; i < count; i++)
            parts[i].RestoreFrom(ref nativeParts[i]);
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
