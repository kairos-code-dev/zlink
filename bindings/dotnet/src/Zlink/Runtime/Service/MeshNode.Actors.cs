// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class MeshNode
{
    public ActorRef CreateActor(string actorId,
        IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default)
    {
        ActorContractValidation.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkActorRef actorOut = default;
        int rc;
        if (creationParts == null || creationParts.Count == 0)
        {
            rc = NativeMethods.zlink_mesh_node_actor_new(Handle, actorId,
                IntPtr.Zero, 0, out actorOut, 0, timeoutMs);
        }
        else
        {
            var captured = 0;
            ZlinkActorRef local = default;
            NativeMessageParts.SubmitClonedVector(creationParts,
                nameof(creationParts), (np, count) =>
                {
                    var r = NativeMethods.zlink_mesh_node_actor_new(Handle,
                        actorId, np, count, out local, 0, timeoutMs);
                    captured = r;
                    return r;
                }, null);
            rc = captured;
            actorOut = local;
        }

        if (rc != 0)
            throw new ZlinkRequestException((RequestResult)rc);
        return ActorInterop.FromNative(ref actorOut);
    }

    public bool ActorLookup(string actorId, out ActorLocation location)
    {
        ActorContractValidation.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_mesh_node_actor_lookup(Handle, actorId,
            out var native);
        if (rc == (int)ConfigResult.NotFound)
        {
            location = null!;
            return false;
        }

        ZlinkException.ThrowConfigIfError(rc);
        location = new ActorLocation(
            ActorInterop.FromNative(ref native.Actor),
            RoutingId.FromNative(ref native.SpotRid) ?? default,
            native.SpotGeneration,
            native.MembershipEpoch);
        return true;
    }

    public MeshOperationId ActorLookupRemote(RoutingId targetNodeRid,
        string actorId, TimeSpan timeout = default)
    {
        ActorContractValidation.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        var nativeNode = targetNodeRid.ToNative();
        var rc = NativeMethods.zlink_mesh_node_actor_lookup_remote(Handle,
            ref nativeNode, actorId, out var op, MeshSend.EncodeTimeout(timeout));
        ZlinkException.ThrowSubmitIfError(rc);
        return new MeshOperationId(op.High, op.Low);
    }

    public MeshOperationId DestroyActor(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_mesh_node_actor_destroy(Handle,
            ref nativeActor, out var op, MeshSend.EncodeTimeout(timeout));
        ZlinkException.ThrowSubmitIfError(rc);
        return new MeshOperationId(op.High, op.Low);
    }

    public MeshOperationId JoinSpot(ActorRef actor, RoutingId targetNodeRid,
        RoutingId targetSpotRid, ulong targetSpotGeneration,
        IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var nativeNode = targetNodeRid.ToNative();
        var nativeSpot = targetSpotRid.ToNative();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        int rc;
        if (creationParts == null || creationParts.Count == 0)
        {
            rc = NativeMethods.zlink_mesh_node_actor_join_spot(Handle,
                ref nativeActor, ref nativeNode, ref nativeSpot,
                targetSpotGeneration, IntPtr.Zero, 0, out op, timeoutMs);
        }
        else
        {
            var captured = 0;
            NativeMessageParts.SubmitClonedVector(creationParts,
                nameof(creationParts), (np, count) =>
                {
                    var r = NativeMethods.zlink_mesh_node_actor_join_spot(Handle,
                        ref nativeActor, ref nativeNode, ref nativeSpot,
                        targetSpotGeneration, np, count, out op, timeoutMs);
                    captured = r;
                    return r;
                }, null);
            rc = captured;
        }

        ZlinkException.ThrowSubmitIfError(rc);
        return new MeshOperationId(op.High, op.Low);
    }

    public MeshOperationId JoinEntrySpot(ActorRef actor, RoutingId targetNodeRid,
        IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var nativeNode = targetNodeRid.ToNative();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        int rc;
        if (creationParts == null || creationParts.Count == 0)
        {
            rc = NativeMethods.zlink_mesh_node_actor_join_entry_spot(Handle,
                ref nativeActor, ref nativeNode, IntPtr.Zero, 0, out op,
                timeoutMs);
        }
        else
        {
            var captured = 0;
            NativeMessageParts.SubmitClonedVector(creationParts,
                nameof(creationParts), (np, count) =>
                {
                    var r = NativeMethods.zlink_mesh_node_actor_join_entry_spot(
                        Handle, ref nativeActor, ref nativeNode, np, count,
                        out op, timeoutMs);
                    captured = r;
                    return r;
                }, null);
            rc = captured;
        }

        ZlinkException.ThrowSubmitIfError(rc);
        return new MeshOperationId(op.High, op.Low);
    }

    public MeshOperationId LeaveSpot(ActorRef actor,
        ulong expectedMembershipEpoch, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_mesh_node_actor_leave_spot(Handle,
            ref nativeActor, expectedMembershipEpoch, out var op,
            MeshSend.EncodeTimeout(timeout));
        ZlinkException.ThrowSubmitIfError(rc);
        return new MeshOperationId(op.High, op.Low);
    }

    public SubmitResult SendToActor(ActorRef actor,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        return MeshSend.Submit(parts, nameof(parts), (np, count) =>
            NativeMethods.zlink_mesh_node_send_to_actor(Handle, ref nativeActor,
                IntPtr.Zero, np, count, (int)flags));
    }

    public SubmitResult RequestToActor(ActorRef actor,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        var result = MeshSend.Submit(parts, nameof(parts), (np, count) =>
            NativeMethods.zlink_mesh_node_request_to_actor(Handle,
                ref nativeActor, IntPtr.Zero, np, count, out op, (int)flags,
                timeoutMs));
        operationId = new MeshOperationId(op.High, op.Low);
        return result;
    }

    public SubmitResult SendBoundSession(ActorRef actor,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        return MeshSend.Submit(parts, nameof(parts), (np, count) =>
            NativeMethods.zlink_mesh_node_actor_send_bound_session(Handle,
                ref nativeActor, np, count, (int)flags));
    }

    public MeshOperationId CloseBoundSession(ActorRef actor,
        ulong expectedBindingGeneration, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_mesh_node_actor_close_bound_session(Handle,
            ref nativeActor, expectedBindingGeneration, out var op,
            MeshSend.EncodeTimeout(timeout));
        ZlinkException.ThrowSubmitIfError(rc);
        return new MeshOperationId(op.High, op.Low);
    }
}
