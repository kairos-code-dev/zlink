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

internal sealed class Socket : SocketBase
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

    internal SocketType Type => Kernel.Type;

    public void AttachStreamRaw(StreamPacketHandler handler)
    {
        Kernel.AttachStreamRaw(handler);
    }

    public void DetachStream()
    {
        Kernel.DetachStream();
    }

    public void Send(Message message)
    {
        Kernel.Send(message);
    }

    public void Send(IReadOnlyList<Message> parts)
    {
        Kernel.Send(parts);
    }

    public SendResult TrySend(Message message)
    {
        return Kernel.TrySend(message);
    }

    public SendResult TrySend(IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(parts);
    }

    public void Send(string routingId, Message message)
    {
        Kernel.Send(routingId, message);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts)
    {
        Kernel.Send(routingId, parts);
    }

    public SendResult TrySend(string routingId, Message message)
    {
        return Kernel.TrySend(routingId, message);
    }

    public SendResult TrySend(string routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(routingId, parts);
    }

    public void Publish(string topic, Message message)
    {
        Kernel.Publish(topic, message);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts)
    {
        Kernel.Publish(topic, parts);
    }

    public SendResult TryPublish(string topic, Message message)
    {
        return Kernel.TryPublish(topic, message);
    }

    public SendResult TryPublish(string topic, IReadOnlyList<Message> parts)
    {
        return Kernel.TryPublish(topic, parts);
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

    public Subscribed Subscribe()
    {
        return Kernel.Subscribe();
    }

    public Subscribed? TrySubscribe()
    {
        return Kernel.TrySubscribe();
    }

    public void SubscribeHandler(SocketSubscribeHandler handler)
    {
        Kernel.SubscribeHandler(handler);
    }

    public SubscriptionEvent ReceiveSubscriptionEvent()
    {
        return Kernel.ReceiveSubscriptionEvent();
    }

    public SubscriptionEvent? TryReceiveSubscriptionEvent()
    {
        return Kernel.TryReceiveSubscriptionEvent();
    }

    public Received Receive()
    {
        return Kernel.Receive();
    }

    public Received? TryReceive()
    {
        return Kernel.TryReceive();
    }

    public Received ReceiveRouted()
    {
        return Kernel.ReceiveRouted();
    }

    public Received? TryReceiveRouted()
    {
        return Kernel.TryReceiveRouted();
    }
}
