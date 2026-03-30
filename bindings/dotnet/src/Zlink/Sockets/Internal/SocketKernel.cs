// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Zlink.Native;
using Zlink.Service;

namespace Zlink.Sockets.Internal;

internal sealed class SocketKernel : IDisposable
{
    private const int StackSendPartLimit = 8;
    private const int TopicBufferSize = 4096;

    private readonly SocketHandle _handle;
    private readonly SocketType _socketType;
    private NativeMethods.ZlinkStreamOnRawDelegate? _streamRawCallback;
    private StreamPacketHandler? _streamPacketHandler;
    private SocketRecvHandler? _recvHandler;
    private SocketSubscribeHandler? _subscribeHandler;
    private Action? _sendReadyHandler;
    private NativeMethods.ZlinkSocketMsgHandlerDelegate? _recvHandlerNative;
    private NativeMethods.ZlinkSubscribeHandlerDelegate? _subscribeHandlerNative;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private bool _streamAttached;

    public SocketKernel(Context context, SocketType type)
    {
        _handle = new SocketHandle(context, type);
        _socketType = type;
    }

    public SocketKernel(IntPtr handle, bool own)
    {
        _handle = new SocketHandle(handle, own);
        _socketType = ReadSocketType(_handle.DangerousGetHandle());
    }

    public IntPtr Handle => _handle.DangerousGetHandle();

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
        EnsureSupports(SocketCapability.StreamAttach, nameof(AttachStreamRaw));
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        _streamPacketHandler = handler;
        _streamRawCallback = OnStreamRaw;
        int rc = NativeMethods.zlink_stream_attach_raw(Handle,
            _streamRawCallback, IntPtr.Zero);
        if (rc != 0)
        {
            _streamPacketHandler = null;
            _streamRawCallback = null;
            throw ZlinkException.FromLastError();
        }
        _streamAttached = true;
    }

    public void DetachStream()
    {
        EnsureSupports(SocketCapability.StreamAttach, nameof(DetachStream));
        if (!_streamAttached)
            return;

        int rc = NativeMethods.zlink_stream_detach(Handle);
        _streamAttached = false;
        _streamPacketHandler = null;
        _streamRawCallback = null;
        ZlinkException.ThrowIfError(rc);
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(SocketCapability.MessageSend, nameof(Send));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        SendSingleCore(message, flags);
    }

    public void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureSupports(SocketCapability.MessageSend, nameof(Send));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

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

    public void Send(string routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(SocketCapability.RoutedSend, nameof(Send));
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (message == null)
            throw new ArgumentNullException(nameof(message));

        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(encoded);
        SendSingleCore(ref nativeRoutingId, message, flags);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(SocketCapability.RoutedSend, nameof(Send));
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

    public void Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(SocketCapability.Publish, nameof(Publish));
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        PublishSingleCore(topic, message, flags);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureSupports(SocketCapability.Publish, nameof(Publish));
        if (topic == null)
            throw new ArgumentNullException(nameof(topic));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

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

    public void SetSubscription(string topicOrPattern)
    {
        EnsureSupports(SocketCapability.SubscriptionControl,
            nameof(SetSubscription));
        if (topicOrPattern == null)
            throw new ArgumentNullException(nameof(topicOrPattern));

        int rc = NativeMethods.zlink_set_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        EnsureSupports(SocketCapability.SubscriptionControl,
            nameof(UnsetSubscription));
        if (topicOrPattern == null)
            throw new ArgumentNullException(nameof(topicOrPattern));

        int rc = NativeMethods.zlink_unset_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void RecvHandler(SocketRecvHandler handler)
    {
        EnsureSupports(SocketCapability.ReceiveHandler, nameof(RecvHandler));
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        var native = new NativeMethods.ZlinkSocketMsgHandlerDelegate(
            OnNativeReceive);
        int rc = NativeMethods.zlink_recv_handler(Handle, native, IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        _recvHandler = handler;
        _recvHandlerNative = native;
    }

    public void SendReadyHandler(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(Handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerNative = native;
    }

    public void Subscribe(out string topic, out Message message,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.SubscribeReceive, nameof(Subscribe));
        SubscribeCore(out topic, out Message[] parts, flags);
        if (parts.Length != 1)
        {
            foreach (Message part in parts)
                part.Dispose();
            throw new InvalidOperationException(
                "Subscribe(out string, out Message) requires a single-part payload.");
        }

        message = parts[0];
    }

    public void Subscribe(out string topic, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.SubscribeReceive, nameof(Subscribe));
        SubscribeCore(out topic, out parts, flags);
    }

    public void Subscribe(out string routingId, out string topic,
        out Message message, ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.SubscribeReceive, nameof(Subscribe));
        Subscribe(out routingId, out topic, out Message[] parts, flags);
        if (parts.Length != 1)
        {
            foreach (Message part in parts)
                part.Dispose();
            throw new InvalidOperationException(
                "Subscribe(out string, out string, out Message) requires a single-part payload.");
        }

        message = parts[0];
    }

    public unsafe void Subscribe(out string routingId, out string topic,
        out Message[] parts, ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.SubscribeReceive, nameof(Subscribe));
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_subscribe(Handle, (IntPtr)(&nativeRoutingId),
                out nativeParts, out partCount, topicBuffer, ref topicLength,
                (int)flags);
            ZlinkException.ThrowIfError(rc);

            routingId = RoutingIdCodec.ToPublicString(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            topic = DecodeTopic(topicBuffer, topicLength);
            parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
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

    public unsafe void SubscribeHandler(SocketSubscribeHandler handler)
    {
        EnsureSupports(SocketCapability.SubscribeHandler,
            nameof(SubscribeHandler));
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        var native = new NativeMethods.ZlinkSubscribeHandlerDelegate(
            OnNativeSubscribe);
        int rc = NativeMethods.zlink_subscribe_handler(Handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        _subscribeHandler = handler;
        _subscribeHandlerNative = native;
    }

    public void ReceiveSubscriptionEvent(out string topic, out bool subscribed,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.SubscriptionEvents,
            nameof(ReceiveSubscriptionEvent));
        ReceiveSubscriptionEvent(out _, out topic, out subscribed, flags);
    }

    public unsafe void ReceiveSubscriptionEvent(out string routingId,
        out string topic, out bool subscribed, ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.SubscriptionEvents,
            nameof(ReceiveSubscriptionEvent));
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_subscription_event(Handle,
                (IntPtr)(&nativeRoutingId), out int subscribedInt, topicBuffer,
                ref topicLength, (int)flags);
            ZlinkException.ThrowIfError(rc);

            routingId = RoutingIdCodec.ToPublicString(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            topic = DecodeTopic(topicBuffer, topicLength);
            subscribed = subscribedInt != 0;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    public void Receive(out Message message, ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.MessageReceive, nameof(Receive));
        ReceiveCore(out Message[] parts, flags);
        if (parts.Length != 1)
        {
            foreach (Message part in parts)
                part.Dispose();
            throw new InvalidOperationException(
                "Receive(out Message) requires a single-part payload.");
        }

        message = parts[0];
    }

    public void Receive(out Message[] parts, ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.MessageReceive, nameof(Receive));
        ReceiveCore(out parts, flags);
    }

    public void Receive(out string routingId, out Message message,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.RoutedReceive, nameof(Receive));
        Receive(out routingId, out Message[] parts, flags);
        if (parts.Length != 1)
        {
            foreach (Message part in parts)
                part.Dispose();
            throw new InvalidOperationException(
                "Receive(out string, out Message) requires a single-part payload.");
        }

        message = parts[0];
    }

    public void Receive(out string routingId, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureSupports(SocketCapability.RoutedReceive, nameof(Receive));
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        ZlinkRoutingId nativeRoutingId = default;
        try
        {
            int rc = NativeMethods.zlink_recv(Handle, out nativeRoutingId,
                out nativeParts, out partCount, (int)flags);
            ZlinkException.ThrowIfError(rc);
            parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
            routingId = RoutingIdCodec.ToPublicString(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
        }
        catch
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            throw;
        }
    }

    public void SetOption(SocketOptionKey<int> option, int value)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        SetOptionInt32(option.Option, value);
    }

    public void SetOption(SocketOptionKey<long> option, long value)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        SetOptionInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        SetOptionUInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(option, value.AsSpan());
    }

    public void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        SetOptionBytes(option.Option, value);
    }

    public void SetOption(SocketOptionKey<string> option, string value)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        SetOptionString(option.Option, value);
    }

    public int GetOption(SocketOptionKey<int> option)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        return GetOptionInt32(option.Option);
    }

    public long GetOption(SocketOptionKey<long> option)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        return GetOptionInt64(option.Option);
    }

    public ulong GetOption(SocketOptionKey<ulong> option)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        return GetOptionUInt64(option.Option);
    }

    public byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        return GetOptionBytes(option.Option, initialSize);
    }

    public int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        return GetOptionBytesInto(option.Option, destination);
    }

    public string GetOption(SocketOptionKey<string> option, int initialSize = 256)
    {
        EnsureOptionSupported(option.Option, nameof(option));
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        return GetOptionString(option.Option, initialSize);
    }

    public SocketMonitor OpenMonitor(SocketEvent events)
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
        }

        _handle.Dispose();
        _recvHandler = null;
        _subscribeHandler = null;
        _sendReadyHandler = null;
        _recvHandlerNative = null;
        _subscribeHandlerNative = null;
        _sendReadyHandlerNative = null;
        GC.SuppressFinalize(this);
    }

    private unsafe void SetOptionInt32(SocketOption option, int value)
    {
        int tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = SetOptionCore(option, ptr, (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(SocketOption option, long value)
    {
        long tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = SetOptionCore(option, ptr, (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(SocketOption option, ulong value)
    {
        ulong tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = SetOptionCore(option, ptr, (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(SocketOption option, ReadOnlySpan<byte> value)
    {
        if (option == SocketOption.RoutingId)
        {
            int rc;
            fixed (byte* ptr = value)
            {
                rc = NativeMethods.zlink_set_routing_id(Handle, (IntPtr)ptr,
                    (nuint)value.Length);
            }
            ZlinkException.ThrowIfError(rc);
            return;
        }

        fixed (byte* ptr = value)
        {
            int rc = SetOptionCore(option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(SocketOption option, string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        if (option == SocketOption.Subscribe)
        {
            int rc = NativeMethods.zlink_set_subscription(Handle, value);
            ZlinkException.ThrowIfError(rc);
            return;
        }

        if (option == SocketOption.Unsubscribe)
        {
            int rc = NativeMethods.zlink_unset_subscription(Handle, value);
            ZlinkException.ThrowIfError(rc);
            return;
        }

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetOptionBytes(option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetOptionBytes(option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    private unsafe int GetOptionInt32(SocketOption option)
    {
        int value = 0;
        nuint size = (nuint)sizeof(int);
        IntPtr ptr = new IntPtr(&value);
        int rc = GetOptionCore(option, ptr, ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    private unsafe long GetOptionInt64(SocketOption option)
    {
        long value = 0;
        nuint size = (nuint)sizeof(long);
        IntPtr ptr = new IntPtr(&value);
        int rc = GetOptionCore(option, ptr, ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    private unsafe ulong GetOptionUInt64(SocketOption option)
    {
        ulong value = 0;
        nuint size = (nuint)sizeof(ulong);
        IntPtr ptr = new IntPtr(&value);
        int rc = GetOptionCore(option, ptr, ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    private byte[] GetOptionBytes(SocketOption option, int initialSize = 256)
    {
        if (option == SocketOption.RoutingId)
        {
            int rc = NativeMethods.zlink_get_routing_id(Handle, out var routingId);
            ZlinkException.ThrowIfError(rc);
            return NativeHelpers.ReadRoutingId(ref routingId);
        }

        if (initialSize <= 0)
            throw new ArgumentOutOfRangeException(nameof(initialSize));
        byte[] rented = ArrayPool<byte>.Shared.Rent(initialSize);
        try
        {
            while (true)
            {
                unsafe
                {
                    fixed (byte* ptr = rented)
                    {
                        nuint size = (nuint)rented.Length;
                        int rc = GetOptionCore(option, (IntPtr)ptr, ref size);
                        if (rc == 0)
                        {
                            int actual = checked((int)size);
                            byte[] result = new byte[actual];
                            if (actual > 0)
                            {
                                int toCopy = actual;
                                if (toCopy > rented.Length)
                                    toCopy = rented.Length;
                                Array.Copy(rented, result, toCopy);
                            }
                            return result;
                        }

                        if (size > (nuint)rented.Length)
                        {
                            ArrayPool<byte>.Shared.Return(rented);
                            rented = ArrayPool<byte>.Shared.Rent(checked((int)size));
                            continue;
                        }

                        ZlinkException.ThrowIfError(rc);
                    }
                }
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    private unsafe int GetOptionBytesInto(SocketOption option, Span<byte> destination)
    {
        fixed (byte* ptr = destination)
        {
            nuint size = (nuint)destination.Length;
            int rc = GetOptionCore(option, (IntPtr)ptr, ref size);
            ZlinkException.ThrowIfError(rc);
            return checked((int)size);
        }
    }

    private int SetOptionCore(SocketOption option, IntPtr value, nuint length)
    {
        int code = (int)option;
        if ((code & 0xFF00) == 0x3100)
            return NativeMethods.zlink_set_router_option(Handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3200)
            return NativeMethods.zlink_set_dealer_option(Handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3300)
            return NativeMethods.zlink_set_pub_option(Handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3400)
            return NativeMethods.zlink_set_sub_option(Handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3500)
            return NativeMethods.zlink_set_stream_option(Handle, code, value,
                length);
        return NativeMethods.zlink_set_option(Handle, code, value, length);
    }

    private int GetOptionCore(SocketOption option, IntPtr value, ref nuint length)
    {
        int code = (int)option;
        if ((code & 0xFF00) == 0x3100)
            return NativeMethods.zlink_get_router_option(Handle, code, value,
                ref length);
        if ((code & 0xFF00) == 0x3300)
            return NativeMethods.zlink_get_pub_option(Handle, code, value,
                ref length);
        if ((code & 0xFF00) == 0x3400)
            return NativeMethods.zlink_get_sub_option(Handle, code, value,
                ref length);
        if ((code & 0xFF00) == 0x3500)
            return NativeMethods.zlink_get_stream_option(Handle, code, value,
                ref length);
        return NativeMethods.zlink_get_option(Handle, code, value, ref length);
    }

    private string GetOptionString(SocketOption option, int initialSize = 256)
    {
        byte[] bytes = GetOptionBytes(option, initialSize);
        int len = Array.IndexOf(bytes, (byte)0);
        if (len < 0)
            len = bytes.Length;
        return Encoding.UTF8.GetString(bytes, 0, len);
    }

    private unsafe void SubscribeCore(out string topic, out Message[] parts,
        ReceiveFlags flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            int rc = NativeMethods.zlink_subscribe(Handle, IntPtr.Zero,
                out nativeParts, out partCount, topicBuffer, ref topicLength,
                (int)flags);
            ZlinkException.ThrowIfError(rc);

            topic = DecodeTopic(topicBuffer, topicLength);
            parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
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

    private void ReceiveCore(out Message[] parts, ReceiveFlags flags)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        try
        {
            int rc = NativeMethods.zlink_recv(Handle, IntPtr.Zero, out nativeParts,
                out partCount, (int)flags);
            ZlinkException.ThrowIfError(rc);
            parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
        }
        catch
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            throw;
        }
    }

    private static SocketType ReadSocketType(IntPtr handle)
    {
        unsafe
        {
            int value = 0;
            nuint size = (nuint)sizeof(int);
            int rc = NativeMethods.zlink_get_option(handle, (int)SocketOption.Type,
                new IntPtr(&value), ref size);
            ZlinkException.ThrowIfError(rc);
            return (SocketType)value;
        }
    }

    private void EnsureSupports(SocketCapability capability, string memberName)
    {
        if (Supports(capability))
            return;

        throw new InvalidOperationException(
            $"Socket type '{_socketType}' does not support '{memberName}'.");
    }

    private bool Supports(SocketCapability capability)
    {
        return capability switch
        {
            SocketCapability.MessageSend => _socketType == SocketType.Pair
                || _socketType == SocketType.Dealer,
            SocketCapability.MessageReceive => _socketType == SocketType.Pair
                || _socketType == SocketType.Dealer,
            SocketCapability.RoutedSend => _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            SocketCapability.RoutedReceive => _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            SocketCapability.Publish => _socketType == SocketType.Pub
                || _socketType == SocketType.XPub,
            SocketCapability.SubscriptionControl => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketCapability.SubscribeReceive => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketCapability.ReceiveHandler => _socketType == SocketType.Pair
                || _socketType == SocketType.Dealer
                || _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            SocketCapability.SubscribeHandler => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketCapability.SubscriptionEvents => _socketType == SocketType.XPub,
            SocketCapability.StreamAttach => _socketType == SocketType.Stream,
            _ => false
        };
    }

    private void EnsureOptionSupported(SocketOption option, string paramName)
    {
        if (IsOptionSupported(option))
            return;

        throw new InvalidOperationException(
            $"Socket option '{option}' is not supported by socket type '{_socketType}'.");
    }

    private bool IsOptionSupported(SocketOption option)
    {
        return option switch
        {
            SocketOption.Subscribe or SocketOption.Unsubscribe
                or SocketOption.OnlyFirstSubscribe => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketOption.StreamNotify => _socketType == SocketType.Stream,
            SocketOption.XPubVerbose or SocketOption.XPubVerboser
                or SocketOption.XPubManual or SocketOption.XPubManualLastValue
                or SocketOption.XPubWelcomeMsg or SocketOption.TopicsCount
                => _socketType == SocketType.XPub,
            SocketOption.XPubNoDrop => _socketType == SocketType.Pub
                || _socketType == SocketType.XPub,
            SocketOption.RouterMandatory or SocketOption.RouterHandover
                => _socketType == SocketType.Router,
            SocketOption.ProbeRouter => _socketType == SocketType.Dealer,
            SocketOption.RoutingId => _socketType == SocketType.Dealer
                || _socketType == SocketType.Router,
            _ => true
        };
    }

    private enum SocketCapability
    {
        MessageSend,
        MessageReceive,
        RoutedSend,
        RoutedReceive,
        Publish,
        SubscriptionControl,
        SubscribeReceive,
        ReceiveHandler,
        SubscribeHandler,
        SubscriptionEvents,
        StreamAttach
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
            return packetHandler(routingIdText, payloadMsg);
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
            handler(routingId, topicId, managedParts);
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
        if (handler == null)
            return;

        try
        {
            handler();
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
            handler(routingId, managedParts);
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

    private unsafe void SendCore(ReadOnlySpan<Message> parts, SendFlags flags,
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
                    (nuint)parts.Length, (int)flags);
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

    private unsafe void SendSingleCore(Message message, SendFlags flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_send(Handle, (IntPtr)(&nativePart), 1,
                (int)flags);
            if (rc < 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
        SendFlags flags, string paramName)
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
                    (nuint)parts.Length, (int)flags);
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

    private unsafe void PublishSingleCore(string topic, Message message,
        SendFlags flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_publish(Handle, topic,
                (IntPtr)(&nativePart), 1, (int)flags);
            if (rc < 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void SendCore(ref ZlinkRoutingId routingId,
        ReadOnlySpan<Message> parts, SendFlags flags, string paramName)
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
                    (IntPtr)nativePtr, (nuint)parts.Length, (int)flags);
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

    private unsafe void SendSingleCore(ref ZlinkRoutingId routingId,
        Message message, SendFlags flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_send_rid(Handle, ref routingId,
                (IntPtr)(&nativePart), 1, (int)flags);
            if (rc < 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
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
