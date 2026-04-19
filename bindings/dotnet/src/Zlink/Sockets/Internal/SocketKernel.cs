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
    private NativeMethods.ZlinkStreamOnPacketDelegate? _streamPacketCallback;
    private StreamPacketHandler? _streamPacketHandler;
    private StreamUInt32PacketHandler? _streamUInt32PacketHandler;
    private StreamFramedPacketHandler? _streamFramedPacketHandler;
    private StreamUInt32FramedPacketHandler? _streamUInt32FramedPacketHandler;
    private SocketRecvHandler? _recvHandler;
    private SocketSubscribeHandler? _subscribeHandler;
    private Action? _sendReadyHandler;
    private SynchronizationContext? _streamRawContext;
    private SynchronizationContext? _streamPacketContext;
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

    public void AttachStreamRaw(StreamUInt32PacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamRaw),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        SynchronizationContext? context = SynchronizationContext.Current;
        _streamUInt32PacketHandler = handler;
        _streamRawContext = context;
        _streamRawCallback = OnStreamRawUInt32;
        int rc = NativeMethods.zlink_stream_attach_raw(Handle,
            _streamRawCallback, IntPtr.Zero);
        if (rc != 0)
        {
            _streamUInt32PacketHandler = null;
            _streamRawContext = null;
            _streamRawCallback = null;
            throw ZlinkException.FromLastError();
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
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        SynchronizationContext? context = SynchronizationContext.Current;
        _streamFramedPacketHandler = handler;
        _streamPacketContext = context;
        _streamPacketCallback = OnStreamPacket;
        int rc = NativeMethods.zlink_stream_packet_handler(Handle,
            _streamPacketCallback, IntPtr.Zero);
        if (rc != 0)
        {
            _streamFramedPacketHandler = null;
            _streamPacketContext = null;
            _streamPacketCallback = null;
            throw ZlinkException.FromLastError();
        }
        _streamAttached = true;
    }

    public void AttachStreamPacket(StreamUInt32FramedPacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamPacket),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        SynchronizationContext? context = SynchronizationContext.Current;
        _streamUInt32FramedPacketHandler = handler;
        _streamPacketContext = context;
        _streamPacketCallback = OnStreamPacketUInt32;
        int rc = NativeMethods.zlink_stream_packet_handler(Handle,
            _streamPacketCallback, IntPtr.Zero);
        if (rc != 0)
        {
            _streamUInt32FramedPacketHandler = null;
            _streamPacketContext = null;
            _streamPacketCallback = null;
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
        _streamUInt32PacketHandler = null;
        _streamFramedPacketHandler = null;
        _streamUInt32FramedPacketHandler = null;
        _streamRawCallback = null;
        _streamPacketCallback = null;
        _streamRawContext = null;
        _streamPacketContext = null;
        ZlinkException.ThrowIfError(rc);
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.MessageSend);
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

    public SendResult SendNoWaitResult(Message message)
    {
        EnsureSupports(nameof(SendNoWaitResult),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return SendSingleNoWaitResultCore(message);
    }

    internal void SendRawSingle(ReadOnlySpan<byte> payload, int flags)
    {
        EnsureSupports(nameof(SendRawSingle),
            SocketTypePolicy.SocketCapability.MessageSend);
        SendRawSingleCore(payload, flags);
    }

    internal SendResult SendRawSingleNoWaitResult(ReadOnlySpan<byte> payload)
    {
        EnsureSupports(nameof(SendRawSingleNoWaitResult),
            SocketTypePolicy.SocketCapability.MessageSend);
        return SendRawSingleNoWaitResultCore(payload);
    }

    internal void SendBorrowedSingle(byte[] payload, int flags)
    {
        EnsureSupports(nameof(SendBorrowedSingle),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        SendBorrowedSingleCore(payload, flags);
    }

    internal SendResult SendBorrowedSingleNoWaitResult(byte[] payload)
    {
        EnsureSupports(nameof(SendBorrowedSingleNoWaitResult),
            SocketTypePolicy.SocketCapability.MessageSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        return SendBorrowedSingleNoWaitResultCore(payload);
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
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
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

    public void Send(uint routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Send), SocketTypePolicy.SocketCapability.RoutedSend);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        byte[] encoded = RoutingIdCodec.FromUInt32(routingId);
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
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return SendSingleNoWaitResultCore(ref nativeRoutingId, message);
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

    internal void SendBorrowedSingle(uint routingId, byte[] payload,
        int flags)
    {
        EnsureSupports(nameof(SendBorrowedSingle),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromUInt32(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendBorrowedSingleCore(ref nativeRoutingId, payload, flags);
    }

    internal void SendBorrowedSingle(ReadOnlySpan<byte> routingId,
        byte[] payload, int flags)
    {
        EnsureSupports(nameof(SendBorrowedSingle),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(routingId);
        SendBorrowedSingleCore(ref nativeRoutingId, payload, flags);
    }

    internal SendResult SendBorrowedSingleNoWaitResult(string routingId, byte[] payload)
    {
        EnsureSupports(nameof(SendBorrowedSingleNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return SendBorrowedSingleNoWaitResultCore(ref nativeRoutingId, payload);
    }

    internal SendResult SendBorrowedSingleNoWaitResult(RoutingId routingId, byte[] payload)
    {
        EnsureSupports(nameof(SendBorrowedSingleNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        return SendBorrowedSingleNoWaitResultCore(ref nativeRoutingId, payload);
    }

    internal SendResult SendBorrowedSingleNoWaitResult(ReadOnlySpan<byte> routingId,
        byte[] payload)
    {
        EnsureSupports(nameof(SendBorrowedSingleNoWaitResult),
            SocketTypePolicy.SocketCapability.RoutedSend);
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(routingId);
        return SendBorrowedSingleNoWaitResultCore(ref nativeRoutingId, payload);
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

        byte[] encoded = RoutingIdCodec.FromRoutingId(routingId);
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

    public void Publish(string topic, Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(nameof(Publish), SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        PublishSingleCore(topic, message, (int)flags);
    }

    internal SendResult PublishNoWaitResult(string topic, Message message)
    {
        EnsureSupports(nameof(PublishNoWaitResult),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return PublishNoWaitSingleCore(topic, message);
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

    internal SendResult PublishRawSingleNoWait(string topic,
        ReadOnlySpan<byte> payload)
    {
        EnsureSupports(nameof(PublishRawSingleNoWait),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        return PublishRawSingleNoWaitCore(topic, payload);
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

    internal SendResult PublishBorrowedSingleNoWait(string topic, byte[] payload)
    {
        EnsureSupports(nameof(PublishBorrowedSingleNoWait),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        return PublishBorrowedSingleNoWaitCore(topic, payload);
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

    internal SendResult PublishNoWaitResult(string topic, IReadOnlyList<Message> parts)
    {
        EnsureSupports(nameof(PublishNoWaitResult),
            SocketTypePolicy.SocketCapability.Publish);
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
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

    internal TopicMessage? SubscribeNoWait()
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

    public SubscriptionEvent? ReceiveSubscriptionEventNoWait()
    {
        EnsureSupports(nameof(ReceiveSubscriptionEvent),
            SocketTypePolicy.SocketCapability.SubscriptionEvents);
        return TryReceiveCore(() => ReceiveSubscriptionEventCore(DontWaitFlag));
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(Recv),
            SocketTypePolicy.SocketCapability.MessageReceive);
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
        return TryReceiveMessageCore(DontWaitFlag);
    }

    public Received ReceiveRouted(RecvFlags flags = RecvFlags.None)
    {
        EnsureSupports(nameof(ReceiveRouted),
            SocketTypePolicy.SocketCapability.RoutedReceive);
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
            for (int i = 0; i < nativeParts.Length; i++)
            {
                int rc = spotRidBytes == null
                    ? NativeMethods.zlink_router_reply_part(Handle,
                        ref nativeRoutingId, requestSequence, ref nativeParts[i],
                        i + 1 < nativeParts.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final)
                    : NativeMethods.zlink_router_reply_spot_part(Handle,
                        ref nativeRoutingId, ref nativeSpotRid, requestSequence,
                        ref nativeParts[i],
                        i + 1 < nativeParts.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
        RequestReplySupport.DisposeParts(cloned);
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
            return new TopicMessage(routingId, null, topic, parts);
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
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_subscription_event(Handle,
                (IntPtr)(&nativeRoutingId), out int subscribedInt, topicBuffer,
                ref topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            RoutingId? routingId = RoutingIdCodec.ToRoutingId(ref nativeRoutingId);
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
        MultipartMessageCollection parts = ReceiveBasicParts(flags, out _)
            ?? throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        return Received.Create((RoutingId?)null, parts);
    }

    private unsafe Received? TryReceiveMessageCore(int flags)
    {
        MultipartMessageCollection? parts = ReceiveBasicParts(flags, out _,
            allowNoData: true);
        return parts == null ? null : Received.Create((RoutingId?)null, parts);
    }

    private unsafe Received ReceiveRoutedCore(int flags)
    {
        MultipartMessageCollection parts = ReceiveRoutedParts(flags,
            out byte[]? routingIdBytes, out byte[]? spotRidBytes,
            out ulong requestSequence) ?? throw ZlinkException.CreateRecvException(
                (int)ErrorCode.EAgain);
        return CreateRoutedReceived(parts, routingIdBytes, spotRidBytes,
            requestSequence);
    }

    private unsafe Received? TryReceiveRoutedCore(int flags)
    {
        MultipartMessageCollection? parts = ReceiveRoutedParts(flags,
            out byte[]? routingIdBytes, out byte[]? spotRidBytes,
            out ulong requestSequence, allowNoData: true);
        return parts == null ? null : CreateRoutedReceived(parts, routingIdBytes,
            spotRidBytes, requestSequence);
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
        byte[]? routingIdBytes, byte[]? spotRidBytes, ulong requestSequence)
    {
        if (requestSequence == 0)
        {
            return Received.Create(routingIdBytes, parts,
                adoptRoutingBytes: true, spotRidBytes: spotRidBytes);
        }

        RoutingId? replyRoutingId = routingIdBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
        RoutingId? replySpotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        return Received.Create(replyRoutingId, parts, requestSequence,
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
                SendReplyCore(replyRoutingId.Value, replySpotRid, requestSequence,
                    copied, sendFlags);
            });
    }

    private unsafe MultipartMessageCollection? ReceiveBasicParts(int flags,
        out byte[]? routingIdBytes, bool allowNoData = false)
    {
        List<ZlinkMsg> nativeParts = new();
        routingIdBytes = null;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                ZlinkException.ThrowIfError(initRc);
                bool initialized = true;
                int rc = NativeMethods.zlink_recv_part(Handle,
                    out IntPtr sourceRoutingId, ref part, out int hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    int errno = NativeMethods.zlink_errno();
                    if (allowNoData && nativeParts.Count == 0
                        && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain) {
                        return null;
                    }
                    throw ZlinkException.CreateRecvException(errno);
                }

                initialized = false;
                routingIdBytes ??= CopyRoutingIdBytes(sourceRoutingId);
                nativeParts.Add(MoveStoredPart(ref part));
                if (hasMore == 0)
                    break;
            }

            return MultipartMessageCollection.FromNativeParts(nativeParts.ToArray());
        }
        catch
        {
            CloseNativeParts(nativeParts);
            throw;
        }
    }

    private unsafe MultipartMessageCollection? ReceiveRoutedParts(int flags,
        out byte[]? routingIdBytes, out byte[]? spotRidBytes,
        out ulong requestSequence, bool allowNoData = false)
    {
        routingIdBytes = null;
        spotRidBytes = null;
        requestSequence = 0;
        if (Type == SocketType.Router)
            return ReceiveRouterParts(flags, out routingIdBytes, out spotRidBytes,
                out requestSequence, allowNoData);

        List<ZlinkMsg> nativeParts = new();
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                ZlinkException.ThrowIfError(initRc);
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
                    if (allowNoData && nativeParts.Count == 0
                        && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain) {
                        return null;
                    }
                    throw ZlinkException.CreateRecvException(errno);
                }

                initialized = false;
                routingIdBytes ??= CopyRoutingIdBytes(sourceNodeRid);
                nativeParts.Add(MoveStoredPart(ref part));
                if (basicHasMore == 0)
                    break;
            }

            return MultipartMessageCollection.FromNativeParts(nativeParts.ToArray());
        }
        catch
        {
            CloseNativeParts(nativeParts);
            throw;
        }
    }

    private unsafe MultipartMessageCollection? ReceiveRouterParts(int flags,
        out byte[]? routingIdBytes, out byte[]? spotRidBytes,
        out ulong requestSequence, bool allowNoData)
    {
        routingIdBytes = null;
        spotRidBytes = null;
        requestSequence = 0;

        int rc = NativeMethods.zlink_router_recv(Handle, out IntPtr sourceNodeRid,
            out IntPtr sourceSpotRid, out requestSequence, out IntPtr parts,
            out nuint partCount, flags);
        if (rc != 0)
        {
            int errno = NativeMethods.zlink_errno();
            if (allowNoData
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
            {
                return null;
            }

            throw ZlinkException.CreateRecvException(errno);
        }

        routingIdBytes = CopyRoutingIdBytes(sourceNodeRid);
        spotRidBytes = CopyRoutingIdBytes(sourceSpotRid);
        Message[] messages = Message.FromNativeVector(parts, partCount);
        return MultipartMessageCollection.FromMessages(messages);
    }

    private unsafe MultipartMessageCollection? ReceiveSubscribedParts(int flags,
        byte[] topicBuffer, out byte[]? routingIdBytes, out string topic,
        bool allowNoData = false)
    {
        List<ZlinkMsg> nativeParts = new();
        routingIdBytes = null;
        topic = string.Empty;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                ZlinkException.ThrowIfError(initRc);
                bool initialized = true;
                int rc = NativeMethods.zlink_subscribe_part(Handle,
                    out IntPtr sourceRoutingId, topicBuffer,
                    (nuint)topicBuffer.Length, out nuint topicLength, ref part,
                    out int hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    int errno = NativeMethods.zlink_errno();
                    if (allowNoData && nativeParts.Count == 0
                        && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain) {
                        return null;
                    }
                    throw ZlinkException.CreateRecvException(errno);
                }

                initialized = false;
                if (nativeParts.Count == 0)
                {
                    routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
                    topic = DecodeTopic(topicBuffer, topicLength);
                }
                nativeParts.Add(MoveStoredPart(ref part));
                if (hasMore == 0)
                    break;
            }

            return MultipartMessageCollection.FromNativeParts(nativeParts.ToArray());
        }
        catch
        {
            CloseNativeParts(nativeParts);
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
                ZlinkException.ThrowIfError(initRc);
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
        out byte[]? spotRidBytes, out ulong requestSequence)
    {
        List<byte[]> frames = new();
        routingIdBytes = null;
        spotRidBytes = null;
        requestSequence = 0;
        while (true)
        {
            ZlinkMsg part = default;
            int initRc = NativeMethods.zlink_msg_init(ref part);
            ZlinkException.ThrowIfError(initRc);
            bool initialized = true;
            int rc;
            IntPtr sourceNodeRid;
            IntPtr sourceSpotRid = IntPtr.Zero;
            int hasMore;
            if (Type == SocketType.Router)
            {
                rc = NativeMethods.zlink_router_recv_part(Handle,
                    out sourceNodeRid, out sourceSpotRid, out requestSequence,
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
        ZlinkException.ThrowIfError(initRc);
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

    private static unsafe void CloseNativeParts(List<ZlinkMsg> parts)
    {
        Span<ZlinkMsg> span = CollectionsMarshal.AsSpan(parts);
        for (int i = 0; i < span.Length; i++)
            NativeMethods.zlink_msg_close(ref span[i]);
    }

    private static unsafe byte[]? CopyRoutingIdBytes(IntPtr routingIdPtr)
    {
        if (routingIdPtr == IntPtr.Zero)
            return null;

        return NativeHelpers.ReadRoutingId(ref *(ZlinkRoutingId*)routingIdPtr);
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

    private static void OnBorrowedBufferFree(IntPtr data, IntPtr hint)
    {
        if (hint == IntPtr.Zero)
            return;
        GCHandle handle = GCHandle.FromIntPtr(hint);
        if (handle.Target is Message)
        {
            Message.CompleteBorrowedSend(handle);
            return;
        }
        handle.Free();
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
            if (context == null)
                return packetHandler(routingIdText, payloadMsg);
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

    private unsafe int OnStreamRawUInt32(IntPtr routingId, IntPtr message,
        IntPtr userdata)
    {
        if (message == IntPtr.Zero)
            return 0;

        StreamUInt32PacketHandler? packetHandler = _streamUInt32PacketHandler;
        SynchronizationContext? context = _streamRawContext;
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

    private unsafe void OnStreamPacket(IntPtr stream, IntPtr routingId,
        IntPtr header, IntPtr body, IntPtr userdata)
    {
        StreamFramedPacketHandler? packetHandler = _streamFramedPacketHandler;
        SynchronizationContext? context = _streamPacketContext;
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
            CallbackDelivery.Post(context,
                () => packetHandler(routingIdText, headerMsg, bodyMsg));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
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
            _streamUInt32FramedPacketHandler;
        SynchronizationContext? context = _streamPacketContext;
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
            CallbackDelivery.Post(context,
                () => packetHandler(routingIdValue, headerMsg, bodyMsg));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
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
        int submitted = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_send_part(Handle,
                    ref nativeParts[i], flags,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, submitted, built - submitted);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult SendNoWaitResultCore(ReadOnlySpan<Message> parts,
        string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_send_part(Handle,
                    ref nativeParts[i], DontWaitFlag,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc == 0)
                    continue;

                RestoreManagedParts(parts, nativeParts, submitted,
                    built - submitted);
                SendResult? sendResult = TryMapSendResultFromErrno();
                if (sendResult != null)
                    return sendResult.Value;
                throw ZlinkException.FromLastError();
            }
            return SendResult.Sent;
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, submitted, built - submitted);
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
        if (message.TryPrepareBorrowedSend(out IntPtr data, out int length,
                out IntPtr hint))
        {
            try
            {
                SendBorrowedSingleCore(data, length, hint, flags);
                message.DetachAfterPreparedSend();
                return;
            }
            catch
            {
                message.CancelBorrowedSendPrepare();
                throw;
            }
        }

        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            moved = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (!moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult SendSingleNoWaitResultCore(Message message)
    {
        if (message.TryPrepareBorrowedSend(out IntPtr data, out int length,
                out IntPtr hint))
        {
            SendResult sendResult = SendBorrowedSingleNoWaitResultCore(data,
                length, hint);
            if (sendResult == SendResult.Sent)
            {
                message.DetachAfterPreparedSend();
            }
            else
            {
                message.CancelBorrowedSendPrepare();
            }
            return sendResult;
        }

        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart,
                DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            moved = true;
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
                throw ZlinkException.FromLastError();
            return sendResult.Value;
        }
        catch
        {
            if (!moved)
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

            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult SendRawSingleNoWaitResultCore(ReadOnlySpan<byte> payload)
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

            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart,
                DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

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
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            SendBorrowedSingleCore(handle.AddrOfPinnedObject(), payload.Length,
                GCHandle.ToIntPtr(handle), flags);
            handle = default;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe void SendBorrowedSingleCore(IntPtr data, int length,
        IntPtr hint, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                data, (nuint)length, BorrowedBufferFreePtr, hint);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult SendBorrowedSingleNoWaitResultCore(byte[] payload)
    {
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            SendResult result = SendBorrowedSingleNoWaitResultCore(
                handle.AddrOfPinnedObject(), payload.Length,
                GCHandle.ToIntPtr(handle));
            if (result != SendResult.Sent && handle.IsAllocated)
            {
                handle.Free();
                handle = default;
            }
            return result;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult SendBorrowedSingleNoWaitResultCore(IntPtr data,
        int length, IntPtr hint)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                data, (nuint)length, BorrowedBufferFreePtr, hint);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            int rc = NativeMethods.zlink_send_part(Handle, ref nativePart,
                DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

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

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_publish_part(Handle, topic,
                    ref nativeParts[i], flags,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, submitted, built - submitted);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult PublishNoWaitCore(string topic,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_publish_part(Handle, topic,
                    ref nativeParts[i], DontWaitFlag,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc == 0)
                    continue;

                RestoreManagedParts(parts, nativeParts, submitted,
                    built - submitted);
                SendResult? sendResult = TryMapSendResultFromErrno();
                if (sendResult != null)
                    return sendResult.Value;
                throw ZlinkException.FromLastError();
            }
            return SendResult.Sent;
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, submitted, built - submitted);
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
            int rc = NativeMethods.zlink_publish_part(Handle, topic,
                ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            moved = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (!moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult PublishNoWaitSingleCore(string topic, Message message)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_publish_part(Handle, topic,
                ref nativePart, DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            moved = true;
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
                throw ZlinkException.FromLastError();
            return sendResult.Value;
        }
        catch
        {
            if (!moved)
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

            int rc = NativeMethods.zlink_publish_part(Handle, topic,
                ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult PublishRawSingleNoWaitCore(string topic,
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

            int rc = NativeMethods.zlink_publish_part(Handle, topic,
                ref nativePart, DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

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

            int rc = NativeMethods.zlink_publish_part(Handle, topic,
                ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
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

    private unsafe SendResult PublishBorrowedSingleNoWaitCore(string topic,
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

            int rc = NativeMethods.zlink_publish_part(Handle, topic,
                ref nativePart, DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

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
        int submitted = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_send_part_rid(Handle,
                    ref routingId, ref nativeParts[i], flags,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, submitted, built - submitted);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult SendNoWaitResultCore(ref ZlinkRoutingId routingId,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            PrepareNativeParts(parts, nativeParts, paramName, ref built);
            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_send_part_rid(Handle,
                    ref routingId, ref nativeParts[i], DontWaitFlag,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc == 0)
                    continue;

                RestoreManagedParts(parts, nativeParts, submitted,
                    built - submitted);
                SendResult? sendResult = TryMapSendResultFromErrno();
                if (sendResult != null)
                    return sendResult.Value;
                throw ZlinkException.FromLastError();
            }
            return SendResult.Sent;
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, submitted, built - submitted);
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
        if (message.TryPrepareBorrowedSend(out IntPtr data, out int length,
                out IntPtr hint))
        {
            try
            {
                SendBorrowedSingleCore(ref routingId, data, length, hint,
                    flags);
                message.DetachAfterPreparedSend();
                return;
            }
            catch
            {
                message.CancelBorrowedSendPrepare();
                throw;
            }
        }

        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            moved = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (!moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult SendSingleNoWaitResultCore(ref ZlinkRoutingId routingId,
        Message message)
    {
        if (message.TryPrepareBorrowedSend(out IntPtr data, out int length,
                out IntPtr hint))
        {
            SendResult sendResult = SendBorrowedSingleNoWaitResultCore(
                ref routingId, data, length, hint);
            if (sendResult == SendResult.Sent)
            {
                message.DetachAfterPreparedSend();
            }
            else
            {
                message.CancelBorrowedSendPrepare();
            }
            return sendResult;
        }

        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                ref nativePart, DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            moved = true;
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
                throw ZlinkException.FromLastError();
            return sendResult.Value;
        }
        catch
        {
            if (!moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void SendBorrowedSingleCore(ref ZlinkRoutingId routingId,
        byte[] payload, int flags)
    {
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            SendBorrowedSingleCore(ref routingId, handle.AddrOfPinnedObject(),
                payload.Length, GCHandle.ToIntPtr(handle), flags);
            handle = default;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe void SendBorrowedSingleCore(ref ZlinkRoutingId routingId,
        IntPtr data, int length, IntPtr hint, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                data, (nuint)length, BorrowedBufferFreePtr, hint);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            int rc = NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult SendBorrowedSingleNoWaitResultCore(
        ref ZlinkRoutingId routingId, byte[] payload)
    {
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            SendResult result = SendBorrowedSingleNoWaitResultCore(ref routingId,
                handle.AddrOfPinnedObject(), payload.Length,
                GCHandle.ToIntPtr(handle));
            if (result != SendResult.Sent && handle.IsAllocated)
            {
                handle.Free();
                handle = default;
            }
            return result;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult SendBorrowedSingleNoWaitResultCore(
        ref ZlinkRoutingId routingId, IntPtr data, int length, IntPtr hint)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                data, (nuint)length, BorrowedBufferFreePtr, hint);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            int rc = NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                ref nativePart, DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

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
        Span<ZlinkMsg> nativeParts, int start, int count)
    {
        for (int i = start + count - 1; i >= start; i--)
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
