// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class StreamSocket : RoutedMessageSocketBase, IStreamSocket
{
    private RoutingId? _routingId;

    public StreamSocket(Context context)
        : base(context, SocketType.Stream)
    {
        Options = new StreamSocketOptions(this);
    }

    public new StreamSocketOptions Options { get; }

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

    /// <summary>
    ///     Async Actor bind (operation builder).
    /// </summary>
    public ActorBindOperation BindActor(RoutingId sessionRid, ActorRef actor)
    {
        return new ActorBindOperationImpl(this, sessionRid, actor);
    }

    /// <summary>
    ///     Async Actor unbind (operation builder).
    /// </summary>
    public ActorUnbindOperation UnbindActor(RoutingId sessionRid,
        string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        return new ActorUnbindOperationImpl(this, sessionRid, actorId);
    }

    /// <summary>
    ///     Session-bound relay send (operation builder).
    /// </summary>
    public SendOperation SendBoundActor(RoutingId sessionRid, string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        return new StreamSendOperation(this, sessionRid, actorId);
    }

    /// <summary>
    ///     Snapshot of Actor refs attached to the given session (local mapping
    ///     only).
    /// </summary>
    public IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid)
    {
        var nativeSession = sessionRid.ToNative();
        nuint count = 0;
        var rc = NativeMethods.zlink_stream_bound_actors(Handle,
            ref nativeSession, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<ActorRef>();
        var entrySize = Marshal.SizeOf<ZlinkActorRef>();
        var entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_stream_bound_actors(Handle,
                ref nativeSession, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            var result = new ActorRef[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var native =
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

        var nativeSession = sessionRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
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
            RequestReplySupport.DisposeParts(cloned);
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }
}
