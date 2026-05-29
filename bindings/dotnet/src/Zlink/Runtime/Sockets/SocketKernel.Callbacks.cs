// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;
using System.Threading;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
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
}
