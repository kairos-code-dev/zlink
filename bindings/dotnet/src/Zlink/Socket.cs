// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Zlink.Sockets.Internal;

namespace Zlink;

/// <summary>
/// STREAM callback for per-packet dispatch.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once.
/// </summary>
public delegate int StreamPacketHandler(string routingId, Message payload);
public delegate void SocketRecvHandler(string routingId, Message[] parts);
public delegate void SocketSubscribeHandler(string routingId, string topic,
    Message[] parts);

public sealed class Socket : SocketBase
{
    public Socket(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal Socket(IntPtr handle, bool own)
        : base(new SocketKernel(handle, own))
    {
    }

    internal static Socket Adopt(IntPtr handle, bool own)
    {
        return new Socket(handle, own);
    }

    public void AttachStreamRaw(StreamPacketHandler handler)
    {
        Kernel.AttachStreamRaw(handler);
    }

    public void DetachStream()
    {
        Kernel.DetachStream();
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        Kernel.Send(message, flags);
    }

    public void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        Kernel.Send(parts, flags);
    }

    public void Send(string routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, message, flags);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, parts, flags);
    }

    public void Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Publish(topic, message, flags);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Publish(topic, parts, flags);
    }

    public void SetSubscription(string topicOrPattern)
    {
        Kernel.SetSubscription(topicOrPattern);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        Kernel.UnsetSubscription(topicOrPattern);
    }

    public void RecvHandler(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public void Subscribe(out string topic, out Message message,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out topic, out message, flags);
    }

    public void Subscribe(out string topic, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out topic, out parts, flags);
    }

    public void Subscribe(out string routingId, out string topic,
        out Message message, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out routingId, out topic, out message, flags);
    }

    public void Subscribe(out string routingId, out string topic,
        out Message[] parts, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out routingId, out topic, out parts, flags);
    }

    public void SubscribeHandler(SocketSubscribeHandler handler)
    {
        Kernel.SubscribeHandler(handler);
    }

    public void ReceiveSubscriptionEvent(out string topic, out bool subscribed,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.ReceiveSubscriptionEvent(out topic, out subscribed, flags);
    }

    public void ReceiveSubscriptionEvent(out string routingId, out string topic,
        out bool subscribed, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.ReceiveSubscriptionEvent(out routingId, out topic, out subscribed,
            flags);
    }

    public void Receive(out Message message, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out message, flags);
    }

    public void Receive(out Message[] parts, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out parts, flags);
    }

    public void Receive(out string routingId, out Message message,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out routingId, out message, flags);
    }

    public void Receive(out string routingId, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out routingId, out parts, flags);
    }
}
