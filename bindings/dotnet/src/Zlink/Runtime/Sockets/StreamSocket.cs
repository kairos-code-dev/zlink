// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class StreamSocket : RoutedMessageSocketBase, IStreamSocket
{
    private RoutingId? _routingId;

    public new StreamSocketOptions Options { get; }

    public StreamSocket(Context context)
        : base(context, SocketType.Stream)
    {
        Options = new StreamSocketOptions(this);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        _routingId = routingId;
    }

    public RoutingId GetRoutingId()
    {
        return _routingId ?? throw new ZlinkConfigException(
            ZlinkConfigException.ErrorCode.NotFound);
    }

    public void OnPacket(StreamPacketHandler handler)
    {
        Kernel.AttachStreamPacket(handler);
    }

    public void DisconnectRid(RoutingId peerRid)
    {
        Kernel.DisconnectRid(peerRid);
    }

    public void DetachStream()
    {
        Kernel.DetachStream();
    }

    public void AttachActorGateway(ISpotNode node)
    {
        SpotNode concreteNode = SocketInterop.RequireSpotNode(node,
            nameof(node));
        int rc = NativeMethods.zlink_stream_attach_actor_gateway(Handle,
            concreteNode.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    /// <summary>
    /// Async Actor bind (operation builder).
    /// </summary>
    public ActorBindOperation BindActor(RoutingId sessionRid, ActorRef actor)
    {
        return new ActorBindOperationImpl(this, sessionRid, actor);
    }

    /// <summary>
    /// Async Actor unbind (operation builder).
    /// </summary>
    public ActorUnbindOperation UnbindActor(RoutingId sessionRid,
        string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        return new ActorUnbindOperationImpl(this, sessionRid, actorId);
    }

    /// <summary>
    /// Session-bound relay send (operation builder).
    /// </summary>
    public SendOperation SendBoundActor(RoutingId sessionRid, string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        return new StreamSendOperation(this, sessionRid, actorId);
    }

    /// <summary>
    /// Snapshot of Actor refs attached to the given session (local mapping
    /// only).
    /// </summary>
    public IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid)
    {
        ZlinkRoutingId nativeSession = sessionRid.ToNative();
        nuint count = 0;
        int rc = NativeMethods.zlink_stream_bound_actors(Handle,
            ref nativeSession, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<ActorRef>();
        int entrySize = Marshal.SizeOf<ZlinkActorRef>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_stream_bound_actors(Handle,
                ref nativeSession, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            ActorRef[] result = new ActorRef[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkActorRef native =
                    Marshal.PtrToStructure<ZlinkActorRef>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = ActorInterop.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    internal bool SendBoundActorCore(RoutingId sessionRid, string actorId,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.",
                nameof(parts));

        ZlinkRoutingId nativeSession = sessionRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_stream_send_bound_actor_part(Handle,
                        ref nativeSession, actorId, ref nativePart, (int)flags,
                        partFlag));
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
