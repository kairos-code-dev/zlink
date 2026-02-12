// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using Zlink.Native;

namespace Zlink;

public sealed class SpotNode : IDisposable
{
    private IntPtr _handle;

    public SpotNode(Context context)
    {
        _handle = NativeMethods.zlink_spot_node_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void Bind(string endpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_bind(_handle, endpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectRegistry(string registryEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_registry(_handle,
            registryEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectPeerPub(string peerPubEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer_pub(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void DisconnectPeerPub(string peerPubEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer_pub(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Register(string serviceName, string advertiseEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_register(_handle, serviceName,
            advertiseEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unregister(string serviceName)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_unregister(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetDiscovery(Discovery discovery, string serviceName)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_set_discovery(_handle,
            discovery.Handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsServer(string cert, string key)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_tls_server(_handle, cert,
            key);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCert, string hostname, bool trustSystem)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_tls_client(_handle, caCert,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetSockOpt(SpotNodeSocketRole role, SocketOption option,
        byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetSockOpt(role, option, value.AsSpan());
    }

    public unsafe void SetSockOpt(SpotNodeSocketRole role, SocketOption option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_spot_node_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(SpotNodeSocketRole role, SocketOption option,
        int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_spot_node_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void SetSockOpt(SpotNodeOption option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_spot_node_setsockopt(_handle,
            (int)SpotNodeSocketRole.Node, (int)option, (IntPtr)(&tmp),
            (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
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
}

public sealed class Spot : IDisposable
{
    private const int StackPublishPartLimit = 8;
    private IntPtr _pubHandle;
    private IntPtr _subHandle;

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
        if (topicId == null)
            throw new ArgumentNullException(nameof(topicId));
        if (parts.Length == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

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
                parts[i].CopyTo(ref nativeParts[i]);
                built++;
            }

            fixed (ZlinkMsg* ptr = nativeParts)
            {
                rc = NativeMethods.zlink_spot_pub_publish(_pubHandle, topicId,
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
        if (topicId == null)
            throw new ArgumentNullException(nameof(topicId));

        ZlinkMsg part = default;
        int rc = 0;
        bool built = false;
        try
        {
            Message.InitFromSpan(payload, ref part);
            built = true;
            rc = NativeMethods.zlink_spot_pub_publish(_pubHandle, topicId, &part,
                (nuint)1, (int)flags);
        }
        catch
        {
            if (built)
                NativeMethods.zlink_msg_close(ref part);
            throw;
        }

        if (rc < 0 && built)
            NativeMethods.zlink_msg_close(ref part);
        ZlinkException.ThrowIfError(rc);
    }

    public void Subscribe(string topicId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_subscribe(_subHandle, topicId);
        ZlinkException.ThrowIfError(rc);
    }

    public void SubscribePattern(string pattern)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_subscribe_pattern(_subHandle, pattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unsubscribe(string topicIdOrPattern)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_unsubscribe(_subHandle,
            topicIdOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public SpotMessage Receive(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        unsafe
        {
            byte* topicBuf = stackalloc byte[256];
            nuint topicLen = 256;
            int rc = NativeMethods.zlink_spot_sub_recv(_subHandle, out var parts,
                out var count, (int)flags, topicBuf, ref topicLen);
            if (rc != 0)
                throw ZlinkException.FromLastError();
            string topic = NativeHelpers.ReadString(topicBuf, (int)topicLen);
            Message[] messages = Message.FromNativeVector(parts, count);
            return new SpotMessage(topic, messages);
        }
    }

    public unsafe int ReceiveSinglePayload(Span<byte> payloadBuffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        byte* topicBuf = stackalloc byte[256];
        nuint topicLen = 256;
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
                NativeMethods.zlink_msgv_close(parts, count);
        }
    }

    public bool TryReceiveSinglePayload(Span<byte> payloadBuffer,
        out int payloadSize, ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        return TryReceiveSinglePayload(payloadBuffer, out payloadSize, out _,
            flags);
    }

    public unsafe bool TryReceiveSinglePayload(Span<byte> payloadBuffer,
        out int payloadSize, out int errno,
        ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        EnsureNotDisposed();
        byte* topicBuf = stackalloc byte[256];
        nuint topicLen = 256;
        int rc = NativeMethods.zlink_spot_sub_recv(_subHandle, out var parts,
            out var count, (int)flags, topicBuf, ref topicLen);
        if (rc != 0)
        {
            payloadSize = 0;
            errno = NativeMethods.zlink_errno();
            return false;
        }

        try
        {
            payloadSize = Message.CopySinglePartPayload(parts, count,
                payloadBuffer);
            errno = 0;
            return true;
        }
        finally
        {
            if (parts != IntPtr.Zero && count > 0)
                NativeMethods.zlink_msgv_close(parts, count);
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
}

public readonly struct SpotMessage
{
    public SpotMessage(string topicId, Message[] parts)
    {
        TopicId = topicId;
        Parts = parts;
    }

    public string TopicId { get; }
    public Message[] Parts { get; }
}
