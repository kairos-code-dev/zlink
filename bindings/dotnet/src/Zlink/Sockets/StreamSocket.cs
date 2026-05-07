// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class StreamSocket : RoutedMessageSocketBase
{
    public StreamSocketOptions StreamOptions { get; }

    public StreamSocket(Context context)
        : base(context, SocketType.Stream)
    {
        StreamOptions = new StreamSocketOptions(this);
    }

    public void OnPacket(StreamPacketHandler handler)
    {
        Kernel.AttachStreamPacket(handler);
    }

    public void OnPacket(Func<string, Message, int> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        Kernel.AttachStreamRaw((routingId, payload) =>
            handler(routingId, payload));
    }

    internal void OnPacket(StreamUInt32PacketHandler handler)
    {
        Kernel.AttachStreamRaw(handler);
    }

    internal void OnFramedPacket(StreamFramedPacketHandler handler)
    {
        Kernel.AttachStreamPacket(handler);
    }

    public void OnFramedPacket(Action<string, Message, Message> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        Kernel.AttachStreamPacket((routingId, header, body) =>
            handler(routingId, header, body));
    }

    internal void OnFramedPacket(StreamUInt32FramedPacketHandler handler)
    {
        Kernel.AttachStreamPacket(handler);
    }

    internal void Send(uint routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, message, flags);
    }

    internal void Send(uint routingId, byte[] payload,
        SendFlags flags = SendFlags.None)
    {
        Kernel.SendBorrowedSingle(routingId, payload, (int)flags);
    }

    public void DisconnectRid(RoutingId peerRid)
    {
        Kernel.DisconnectRid(peerRid);
    }

    public void DetachStream()
    {
        Kernel.DetachStream();
    }

    public void BindActor(SpotNode node, RoutingId sessionRid,
        ActorRef actor, TimeSpan timeout = default)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        ZlinkRoutingId nativeSession = NativeHelpers.WriteRoutingId(
            sessionRid.ToByteArray());
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        int rc = NativeMethods.zlink_stream_bind_actor(node.Handle, Handle,
            ref nativeSession, ref nativeActor,
            ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public void UnbindActor(SpotNode node, RoutingId sessionRid,
        string actorId, TimeSpan timeout = default)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        ZlinkRoutingId nativeSession = NativeHelpers.WriteRoutingId(
            sessionRid.ToByteArray());
        int rc = NativeMethods.zlink_stream_unbind_actor(node.Handle, Handle,
            ref nativeSession, actorId, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public bool SendBoundActor(SpotNode node, RoutingId sessionRid,
        string actorId, Message message, SendFlags flags = SendFlags.None)
        => SendBoundActor(node, sessionRid, actorId, new[] { message }, flags);

    public bool SendBoundActor(SpotNode node, RoutingId sessionRid,
        string actorId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.",
                nameof(parts));

        ZlinkRoutingId nativeSession = NativeHelpers.WriteRoutingId(
            sessionRid.ToByteArray());
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_stream_send_bound_actor_part(
                        node.Handle, Handle, ref nativeSession, actorId,
                        ref nativePart, (int)flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            NativeMethods.zlink_errno());
                }
                finally
                {
                    if (!submitted)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }
}
