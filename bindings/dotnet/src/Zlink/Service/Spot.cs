// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class SpotNode : IDisposable
{
    private IntPtr _handle;

    public SpotNode(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_spot_node_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void Bind(string endpoint)
    {
        ValidateNotEmpty(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_bind(_handle, endpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectRegistry(string registryEndpoint)
    {
        ValidateNotEmpty(registryEndpoint, nameof(registryEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_registry(_handle,
            registryEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectPeerPub(string peerPubEndpoint)
    {
        ValidateNotEmpty(peerPubEndpoint, nameof(peerPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer_pub(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void DisconnectPeerPub(string peerPubEndpoint)
    {
        ValidateNotEmpty(peerPubEndpoint, nameof(peerPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer_pub(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Register(string serviceName, string advertiseEndpoint)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        ValidateNotEmpty(advertiseEndpoint, nameof(advertiseEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_register(_handle, serviceName,
            advertiseEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unregister(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_unregister(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetDiscovery(Discovery discovery, string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_set_discovery(_handle,
            discovery.Handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsServer(string cert, string key)
    {
        ValidateNotEmpty(cert, nameof(cert));
        ValidateNotEmpty(key, nameof(key));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_tls_server(_handle, cert,
            key);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCert, string hostname, bool trustSystem)
    {
        ValidateNotEmpty(caCert, nameof(caCert));
        ValidateNotEmpty(hostname, nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_tls_client(_handle, caCert,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        SetOptionInt32(role, option.Option, value);
    }

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<long> option,
        long value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        SetOptionInt64(role, option.Option, value);
    }

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<ulong> option,
        ulong value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        SetOptionUInt64(role, option.Option, value);
    }

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<byte[]> option,
        byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(role, option, value.AsSpan());
    }

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        SetOptionBytes(role, option.Option, value);
    }

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<string> option,
        string value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        SetOptionString(role, option.Option, value);
    }

    public unsafe void SetOption(SpotNodeOption option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_spot_node_setsockopt(_handle,
            (int)SpotNodeSocketRole.Node, (int)option, (IntPtr)(&tmp),
            (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt32(SpotNodeSocketRole role,
        SocketOption option, int value)
    {
        int tmp = value;
        int rc = NativeMethods.zlink_spot_node_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(SpotNodeSocketRole role,
        SocketOption option, long value)
    {
        long tmp = value;
        int rc = NativeMethods.zlink_spot_node_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(SpotNodeSocketRole role,
        SocketOption option, ulong value)
    {
        ulong tmp = value;
        int rc = NativeMethods.zlink_spot_node_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(SpotNodeSocketRole role,
        SocketOption option, ReadOnlySpan<byte> value)
    {
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_spot_node_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(SpotNodeSocketRole role, SocketOption option,
        string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetOptionBytes(role, option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetOptionBytes(role, option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    public Socket GetPubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_node_pub_socket(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public Socket GetSubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_node_sub_socket(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public PeerRecord[] GetPubPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_pub_peers(_handle, IntPtr.Zero,
            ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<PeerRecord>();

        ZlinkPeerInfo[] native = ArrayPool<ZlinkPeerInfo>.Shared.Rent((int)count);
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_pub_peers(_handle, native,
                ref actual);
            ZlinkException.ThrowIfError(rc);

            PeerRecord[] peers = new PeerRecord[(int)actual];
            for (int i = 0; i < peers.Length; i++)
                peers[i] = PeerRecord.FromNative(ref native[i]);
            return peers;
        }
        finally
        {
            ArrayPool<ZlinkPeerInfo>.Shared.Return(native);
        }
    }

    public PeerRecord[] GetSubPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_sub_peers(_handle, IntPtr.Zero,
            ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<PeerRecord>();

        ZlinkPeerInfo[] native = ArrayPool<ZlinkPeerInfo>.Shared.Rent((int)count);
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_sub_peers(_handle, native,
                ref actual);
            ZlinkException.ThrowIfError(rc);

            PeerRecord[] peers = new PeerRecord[(int)actual];
            for (int i = 0; i < peers.Length; i++)
                peers[i] = PeerRecord.FromNative(ref native[i]);
            return peers;
        }
        finally
        {
            ArrayPool<ZlinkPeerInfo>.Shared.Return(native);
        }
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_spot_node_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~SpotNode()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotNode));
    }

    private static void ValidateNotEmpty(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);
    }
}

public sealed class Spot : IDisposable
{
    private const int StackPublishPartLimit = 8;
    private const int TopicBufferSize = 256;
    private IntPtr _pubHandle;
    private IntPtr _subHandle;
    private SpotSubHandler? _subHandler;
    private SpotSubPacketHandler? _subPacketHandler;
    private NativeMethods.ZlinkSpotSubHandlerDelegate? _subHandlerNative;

    public Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        _pubHandle = NativeMethods.zlink_spot_pub_new(node.Handle);
        _subHandle = NativeMethods.zlink_spot_sub_new(node.Handle);
        if (_pubHandle == IntPtr.Zero || _subHandle == IntPtr.Zero)
        {
            if (_pubHandle != IntPtr.Zero)
                NativeMethods.zlink_spot_pub_destroy(ref _pubHandle);
            if (_subHandle != IntPtr.Zero)
                NativeMethods.zlink_spot_sub_destroy(ref _subHandle);
            throw ZlinkException.FromLastError();
        }
    }

    /// <summary>
    /// Publishes multipart message and consumes ownership of all parts.
    /// </summary>
    public void Publish(string topicId, Message[] parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        Publish(topicId, parts.AsSpan(), flags);
    }

    public unsafe void Publish(string topicId, ReadOnlySpan<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        ValidateTopicId(topicId, nameof(topicId));
        if (parts.Length == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        byte[] topicUtf8 = GetTopicUtf8(topicId);

        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int rc = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                    throw new ArgumentException(
                        "Parts must not contain null messages.", nameof(parts));
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            fixed (byte* topicPtr = topicUtf8)
            fixed (ZlinkMsg* ptr = nativeParts)
            {
                rc = NativeMethods.zlink_spot_pub_publish(_pubHandle, topicPtr,
                    ptr, (nuint)nativeParts.Length, (int)flags);
            }
        }
        catch
        {
            for (int i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }

        if (rc < 0)
        {
            for (int i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
        }
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void Publish(string topicId, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        ValidateTopicId(topicId, nameof(topicId));
        byte[] topicUtf8 = GetTopicUtf8(topicId);

        int rc;
        fixed (byte* topicPtr = topicUtf8)
        fixed (byte* payloadPtr = payload)
        {
            rc = NativeMethods.zlink_spot_pub_publish_bytes(_pubHandle, topicPtr,
                payloadPtr, (nuint)payload.Length, (int)flags);
        }
        ZlinkException.ThrowIfError(rc);
    }

    public void Subscribe(string topicId)
    {
        ValidateTopicId(topicId, nameof(topicId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_subscribe(_subHandle, topicId);
        ZlinkException.ThrowIfError(rc);
    }

    public void SubscribePattern(string pattern)
    {
        ValidateTopicId(pattern, nameof(pattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_subscribe_pattern(_subHandle, pattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unsubscribe(string topicIdOrPattern)
    {
        ValidateTopicId(topicIdOrPattern, nameof(topicIdOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_unsubscribe(_subHandle,
            topicIdOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void SetHandler(SpotSubHandler? handler)
    {
        EnsureNotDisposed();
        if (handler == null)
        {
            int clearRc = NativeMethods.zlink_spot_sub_set_handler(_subHandle,
                null, IntPtr.Zero);
            ZlinkException.ThrowIfError(clearRc);
            _subHandler = null;
            _subPacketHandler = null;
            _subHandlerNative = null;
            return;
        }

        SpotSubHandler? previousHandler = _subHandler;
        SpotSubPacketHandler? previousPacketHandler = _subPacketHandler;
        NativeMethods.ZlinkSpotSubHandlerDelegate? previousNative =
            _subHandlerNative;
        NativeMethods.ZlinkSpotSubHandlerDelegate nextNative =
            OnNativeSubMessage;
        _subHandler = handler;
        _subPacketHandler = null;
        _subHandlerNative = nextNative;
        int rc = NativeMethods.zlink_spot_sub_set_handler(_subHandle,
            nextNative, IntPtr.Zero);
        if (rc != 0)
        {
            _subHandler = previousHandler;
            _subPacketHandler = previousPacketHandler;
            _subHandlerNative = previousNative;
            throw ZlinkException.FromLastError();
        }
    }

    public unsafe void SetPacketHandler(SpotSubPacketHandler? handler)
    {
        EnsureNotDisposed();
        if (handler == null)
        {
            int clearRc = NativeMethods.zlink_spot_sub_set_handler(_subHandle,
                null, IntPtr.Zero);
            ZlinkException.ThrowIfError(clearRc);
            _subHandler = null;
            _subPacketHandler = null;
            _subHandlerNative = null;
            return;
        }

        SpotSubHandler? previousHandler = _subHandler;
        SpotSubPacketHandler? previousPacketHandler = _subPacketHandler;
        NativeMethods.ZlinkSpotSubHandlerDelegate? previousNative =
            _subHandlerNative;
        NativeMethods.ZlinkSpotSubHandlerDelegate nextNative =
            OnNativeSubMessage;
        _subHandler = null;
        _subPacketHandler = handler;
        _subHandlerNative = nextNative;
        int rc = NativeMethods.zlink_spot_sub_set_handler(_subHandle,
            nextNative, IntPtr.Zero);
        if (rc != 0)
        {
            _subHandler = previousHandler;
            _subPacketHandler = previousPacketHandler;
            _subHandlerNative = previousNative;
            throw ZlinkException.FromLastError();
        }
    }

    /// <summary>
    /// Receives a topic message.
    /// Message ownership is transferred to the caller.
    /// Caller must dispose each part exactly once.
    /// </summary>
    public SpotMessage Receive(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        unsafe
        {
            byte* topicBuf = stackalloc byte[TopicBufferSize];
            nuint topicLen = TopicBufferSize;
            int rc = NativeMethods.zlink_spot_sub_recv(_subHandle, out var parts,
                out var count, (int)flags, topicBuf, ref topicLen);
            if (rc != 0)
                throw ZlinkException.FromLastError();
            // C API may return actual topic length even when output buffer is
            // fixed-size and truncated.
            int topicReadLen = topicLen > TopicBufferSize
                ? TopicBufferSize
                : (int)topicLen;
            string topic = NativeHelpers.ReadString(topicBuf, topicReadLen);
            Message[] messages = Message.FromNativeVector(parts, count);
            return new SpotMessage(topic, messages);
        }
    }

    public unsafe int ReceiveSinglePayload(Span<byte> payloadBuffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        byte* topicBuf = stackalloc byte[TopicBufferSize];
        nuint topicLen = TopicBufferSize;
        int rc = NativeMethods.zlink_spot_sub_recv(_subHandle, out var parts,
            out var count, (int)flags, topicBuf, ref topicLen);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        try
        {
            return Message.CopySinglePartPayload(parts, count, payloadBuffer);
        }
        finally
        {
            if (parts != IntPtr.Zero && count > 0)
                NativeMethods.zlink_multipart_close(parts, count);
        }
    }

    public void Dispose()
    {
        if (_pubHandle != IntPtr.Zero)
        {
            NativeMethods.zlink_spot_pub_destroy(ref _pubHandle);
            _pubHandle = IntPtr.Zero;
        }
        if (_subHandle != IntPtr.Zero)
        {
            if (_subHandlerNative != null)
            {
                try
                {
                    NativeMethods.zlink_spot_sub_set_handler(_subHandle, null,
                        IntPtr.Zero);
                }
                catch
                {
                }
                _subHandler = null;
                _subPacketHandler = null;
                _subHandlerNative = null;
            }
            NativeMethods.zlink_spot_sub_destroy(ref _subHandle);
            _subHandle = IntPtr.Zero;
        }
        GC.SuppressFinalize(this);
    }

    ~Spot()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_pubHandle == IntPtr.Zero || _subHandle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }

    private unsafe void OnNativeSubMessage(byte* topic, nuint topicLen,
        IntPtr parts, nuint partCount, IntPtr userData)
    {
        SpotSubPacketHandler? packetHandler = _subPacketHandler;
        if (packetHandler != null)
        {
            if (partCount > int.MaxValue || (parts == IntPtr.Zero && partCount > 0))
                return;

            SpotPacketView[]? rented = null;
            int count = (int)partCount;
            Span<SpotPacketView> partViews = count <= 32
                ? stackalloc SpotPacketView[count]
                : (rented = ArrayPool<SpotPacketView>.Shared.Rent(count));
            try
            {
                ReadOnlySpan<byte> topicUtf8 = topic == null || topicLen == 0
                    ? ReadOnlySpan<byte>.Empty
                    : new ReadOnlySpan<byte>(topic, checked((int)topicLen));
                ZlinkMsg* msgv = (ZlinkMsg*)parts;
                for (int i = 0; i < count; i++)
                {
                    IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref msgv[i]);
                    int payloadSize = checked((int)NativeMethods.zlink_msg_size(
                        ref msgv[i]));
                    partViews[i] = new SpotPacketView(payloadPtr, payloadSize);
                }
                packetHandler(topicUtf8, partViews.Slice(0, count));
            }
            catch (Exception ex)
            {
                Runtime.ReportUnhandledCallbackException(ex);
            }
            finally
            {
                if (rented != null)
                    ArrayPool<SpotPacketView>.Shared.Return(rented);
            }
            return;
        }

        SpotSubHandler? handler = _subHandler;
        if (handler == null)
            return;

        Message[]? managedParts = null;
        try
        {
            string topicId = topic == null || topicLen == 0
                ? string.Empty
                : Encoding.UTF8.GetString(
                    new ReadOnlySpan<byte>(topic, checked((int)topicLen)));
            managedParts = Message.CopyFromNativeReadOnlyVector(parts, partCount);
            handler(topicId, managedParts);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
            if (managedParts == null)
                return;
            foreach (Message? part in managedParts)
                part?.Dispose();
        }
    }

    private byte[] GetTopicUtf8(string topicId)
    {
        return EncodeTopicUtf8(topicId);
    }

    private static void ValidateTopicId(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);

        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount == 0 || byteCount > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "UTF-8 length must be between 1 and 255 bytes.");
        }
    }

    private static byte[] EncodeTopicUtf8(string topicId)
    {
        int byteCount = Encoding.UTF8.GetByteCount(topicId);
        byte[] bytes = new byte[byteCount + 1];
        Encoding.UTF8.GetBytes(topicId, bytes.AsSpan(0, byteCount));
        bytes[byteCount] = 0;
        return bytes;
    }
}

/// <summary>
/// Managed subscribe callback.
/// Message payload is copied to managed <see cref="Message"/> instances.
/// The callback owns those managed messages and must dispose each part exactly once.
/// </summary>
public delegate void SpotSubHandler(string topicId, Message[] parts);

/// <summary>
/// Zero-copy subscribe callback. topic/payload spans are only valid
/// during the callback invocation.
/// </summary>
public delegate void SpotSubPacketHandler(ReadOnlySpan<byte> topicUtf8,
    ReadOnlySpan<SpotPacketView> parts);

public readonly struct SpotPacketView
{
    private readonly IntPtr _payload;

    internal SpotPacketView(IntPtr payload, int length)
    {
        _payload = payload;
        Length = length;
    }

    public int Length { get; }

    public unsafe ReadOnlySpan<byte> AsReadOnlySpan()
    {
        if (Length <= 0 || _payload == IntPtr.Zero)
            return ReadOnlySpan<byte>.Empty;
        return new ReadOnlySpan<byte>((void*)_payload, Length);
    }
}

public readonly struct SpotMessage
{
    /// <summary>
    /// Spot receive result.
    /// Message ownership is transferred to the caller.
    /// Caller must dispose each part exactly once.
    /// </summary>
    public SpotMessage(string topicId, Message[] parts)
    {
        TopicId = topicId;
        Parts = parts;
    }

    public string TopicId { get; }
    public Message[] Parts { get; }
}
