// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class Actor : IActor
{
    private readonly SpotNode _node;
    private readonly ActorRef _ref;
    private bool _disposed;

    internal Actor(SpotNode node, ActorRef actorRef)
    {
        _node = node ?? throw new ArgumentNullException(nameof(node));
        _ref = actorRef;
    }

    internal SpotNode Node
    {
        get
        {
            EnsureNotDisposed();
            return _node;
        }
    }

    public ActorRef Ref
    {
        get
        {
            EnsureNotDisposed();
            return _ref;
        }
    }

    /// <summary>
    ///     Async user-Spot join (operation builder).
    /// </summary>
    public ActorJoinOperation Join(ISpot spot)
    {
        var concreteSpot = SocketInterop.RequireSpot(spot, nameof(spot));
        EnsureNotDisposed();
        return new ActorJoinOperationImpl(_node, _ref, _node.RoutingId,
            concreteSpot.RoutingId);
    }

    /// <summary>
    ///     Async Spot leave (operation builder).
    /// </summary>
    public ActorLeaveOperation Leave(ISpot spot)
    {
        var concreteSpot = SocketInterop.RequireSpot(spot, nameof(spot));
        EnsureNotDisposed();
        return new ActorLeaveOperationImpl(_node, _ref, concreteSpot.RoutingId);
    }

    public ActorReceived? Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        return ActorInterop.RecvActor(_node.Handle, _ref, flags);
    }

    /// <summary>
    ///     Actor-to-session relay (operation builder). Multipart payload supported.
    /// </summary>
    public SendOperation SendBoundSession()
    {
        EnsureNotDisposed();
        return new ActorSendBoundSessionOperation(_node, _ref);
    }

    public void CloseBoundSession(TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(_ref);
        var rc = NativeMethods.zlink_spot_node_actor_close_bound_session(
            _node.Handle, ref nativeActor, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public void Close(TimeSpan timeout = default)
    {
        Destroy(true, ActorInterop.NormalizeTimeout(timeout));
    }

    public void Dispose()
    {
        Destroy(true, 0);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    internal bool SendBoundSessionDirect(Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return SendBoundSessionCore(_node, _ref, new[] { message }, flags);
    }

    internal static bool SendBoundSessionCore(SpotNode node, ActorRef actorRef,
        IReadOnlyList<Message> parts, SendFlags flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count != 1)
            throw new ArgumentException(
                "Actor bound-session send requires exactly one message.",
                nameof(parts));

        var cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            ZlinkMsg nativePart = default;
            cloned[0].MoveTo(ref nativePart);
            var submitted = false;
            try
            {
                var nativeActor = ActorInterop.ToNative(actorRef);
                var rc =
                    NativeMethods.zlink_spot_node_actor_send_bound_session_msg(
                        node.Handle, ref nativeActor, ref nativePart, (int)flags);
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

    ~Actor()
    {
        Destroy(false, 0);
    }

    private void EnsureNotDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(Actor));
    }

    private void Destroy(bool throwOnError, uint timeoutMs)
    {
        if (_disposed)
            return;
        var nativeActor = ActorInterop.ToNative(_ref);
        var rc = 0;
        if (throwOnError)
            ActorInterop.SubmitAndWait(_node.Handle, completion =>
                NativeMethods.zlink_spot_node_actor_destroy(_node.Handle,
                    ref nativeActor, ActorInterop.ReplyHandlerPtr, completion,
                    timeoutMs));
        else
            rc = NativeMethods.zlink_spot_node_actor_destroy(_node.Handle,
                ref nativeActor, ActorInterop.NoopReplyHandlerPtr, IntPtr.Zero,
                timeoutMs);
        if (rc == 0)
        {
            _disposed = true;
            return;
        }

        if (throwOnError)
            throw ZlinkException.CreateRequestException(
                NativeMethods.zlink_errno());
    }
}