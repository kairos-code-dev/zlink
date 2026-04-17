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
internal delegate void SocketSubscribeHandler(string routingId, string topic,
    Message[] parts);

internal sealed class Socket : ConnectableSocketBase
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

    internal void SendRawSingle(ReadOnlySpan<byte> payload, int flags)
    {
        Kernel.SendRawSingle(payload, flags);
    }

    internal SendResult TrySendRawSingle(ReadOnlySpan<byte> payload)
    {
        return Kernel.TrySendRawSingle(payload);
    }

    internal void SendBorrowedSingle(byte[] payload, int flags)
    {
        Kernel.SendBorrowedSingle(payload, flags);
    }

    internal SendResult TrySendBorrowedSingle(byte[] payload)
    {
        return Kernel.TrySendBorrowedSingle(payload);
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

    internal void SendBorrowedSingle(string routingId, byte[] payload, int flags)
    {
        Kernel.SendBorrowedSingle(routingId, payload, flags);
    }

    internal void SendBorrowedSingle(RoutingId routingId, byte[] payload, int flags)
    {
        Kernel.SendBorrowedSingle(routingId, payload, flags);
    }

    internal SendResult TrySendBorrowedSingle(string routingId, byte[] payload)
    {
        return Kernel.TrySendBorrowedSingle(routingId, payload);
    }

    internal SendResult TrySendBorrowedSingle(RoutingId routingId, byte[] payload)
    {
        return Kernel.TrySendBorrowedSingle(routingId, payload);
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

    internal void PublishRawSingle(string topic, ReadOnlySpan<byte> payload,
        int flags)
    {
        Kernel.PublishRawSingle(topic, payload, flags);
    }

    internal SendResult TryPublishRawSingle(string topic,
        ReadOnlySpan<byte> payload)
    {
        return Kernel.TryPublishRawSingle(topic, payload);
    }

    internal void PublishBorrowedSingle(string topic, byte[] payload, int flags)
    {
        Kernel.PublishBorrowedSingle(topic, payload, flags);
    }

    internal SendResult TryPublishBorrowedSingle(string topic, byte[] payload)
    {
        return Kernel.TryPublishBorrowedSingle(topic, payload);
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

    public TopicMessage Subscribe()
    {
        return Kernel.Subscribe();
    }

    public TopicMessage? TrySubscribe()
    {
        return Kernel.TrySubscribe();
    }

    internal byte[][]? TryReceiveRawSubscribedFrames(int flags)
    {
        return Kernel.TryReceiveRawSubscribedFrames(flags);
    }

    internal int? TryReceiveRawSubscribedFrame(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        return Kernel.TryReceiveRawSubscribedFrame(destination, flags,
            out pendingFrames);
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

    public Received Recv()
    {
        return Kernel.Recv();
    }

    public Received? TryRecv()
    {
        return Kernel.TryRecv();
    }

    public Received ReceiveRouted()
    {
        return Kernel.ReceiveRouted();
    }

    public Received? TryReceiveRouted()
    {
        return Kernel.TryReceiveRouted();
    }

    internal byte[][]? TryReceiveRawFrames(int flags)
    {
        return Kernel.TryReceiveRawFrames(flags);
    }

    internal int? TryReceiveRawFrame(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        return Kernel.TryReceiveRawFrame(destination, flags, out pendingFrames);
    }

    internal int? TryReceiveRawRoutedFrame(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out byte[][] pendingFrames)
    {
        return Kernel.TryReceiveRawRoutedFrame(routingDestination,
            payloadDestination, flags, out pendingFrames);
    }

}
