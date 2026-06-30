// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    public void AttachStreamRaw(StreamRawPacketHandler handler)
    {
        EnsureSupports(nameof(AttachStreamRaw),
            SocketTypePolicy.SocketCapability.StreamAttach);
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new ZlinkHandlerException(HandlerResult.Busy,
                (int)ErrorCode.EBusy);

        var context = SynchronizationContext.Current;
        _callbacks.StreamPacketHandler = handler;
        _callbacks.StreamRawContext = context;
        _callbacks.StreamRawNative = OnStreamRaw;
        var rc = NativeMethods.zlink_stream_attach_raw(Handle,
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

        var context = SynchronizationContext.Current;
        _callbacks.StreamUInt32PacketHandler = handler;
        _callbacks.StreamRawContext = context;
        _callbacks.StreamRawNative = OnStreamRawUInt32;
        var rc = NativeMethods.zlink_stream_attach_raw(Handle,
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

        var context = SynchronizationContext.Current;
        _callbacks.StreamFramedPacketHandler = handler;
        _callbacks.StreamPacketContext = context;
        _callbacks.StreamPacketNative = OnStreamPacket;
        var rc = NativeMethods.zlink_stream_packet_handler(Handle,
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

        var context = SynchronizationContext.Current;
        _callbacks.StreamUInt32FramedPacketHandler = handler;
        _callbacks.StreamPacketContext = context;
        _callbacks.StreamPacketNative = OnStreamPacketUInt32;
        var rc = NativeMethods.zlink_stream_packet_handler(Handle,
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

        var rc = NativeMethods.zlink_stream_detach(Handle);
        _streamAttached = false;
        _callbacks.ClearStream();
        ZlinkException.ThrowCloseIfError(rc);
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

        var packetHandler = _callbacks.StreamPacketHandler;
        var context = _callbacks.StreamRawContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(message);
            return 0;
        }

        var rid = (ZlinkRoutingId*)routingId;
        Message? payloadMsg = null;
        var delivered = false;
        try
        {
            payloadMsg = Message.MoveFromNativeSingle(message);
            var routingIdText = RoutingIdCodec.ToPublicString(
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
                try
                {
                    payloadMsg.Dispose();
                }
                catch
                {
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

        var packetHandler = _callbacks.StreamUInt32PacketHandler;
        var context = _callbacks.StreamRawContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(message);
            return 0;
        }

        var ridBytes = NativeHelpers.ReadRoutingId(
            ref *(ZlinkRoutingId*)routingId);
        if (!RoutingIdCodec.TryToUInt32(ridBytes, out var routingIdValue))
        {
            CloseStreamPacket(message);
            return 0;
        }

        Message? payloadMsg = null;
        var delivered = false;
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
                try
                {
                    payloadMsg.Dispose();
                }
                catch
                {
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
        var packetHandler = _callbacks.StreamFramedPacketHandler;
        var context = _callbacks.StreamPacketContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
            return;
        }

        Message? headerMsg = null;
        Message? bodyMsg = null;
        var delivered = false;
        try
        {
            var routingIdText = RoutingIdCodec.ToPublicString(
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
        var packetHandler =
            _callbacks.StreamUInt32FramedPacketHandler;
        var context = _callbacks.StreamPacketContext;
        if (packetHandler == null || routingId == IntPtr.Zero)
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
            return;
        }

        var ridBytes = NativeHelpers.ReadRoutingId(
            ref *(ZlinkRoutingId*)routingId);
        if (!RoutingIdCodec.TryToUInt32(ridBytes, out var routingIdValue))
        {
            CloseStreamPacket(header);
            CloseStreamPacket(body);
            return;
        }

        Message? headerMsg = null;
        Message? bodyMsg = null;
        var delivered = false;
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
}