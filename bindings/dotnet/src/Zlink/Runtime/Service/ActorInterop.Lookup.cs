// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static Task<ActorLookupResult> RemoteActorGetRefAsync(
        SpotNode node, RoutingId targetNodeRid, string actorId,
        TimeSpan timeout, CancellationToken ct)
    {
        ValidateActorId(actorId, nameof(actorId));
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeNodeRid = targetNodeRid.ToNative();
        TaskCompletionSource<ActorLookupResult> completion = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            RequestCallState<ActorLookupResult> state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.SetCancellationRegistration(ct.Register(
                    static h =>
                    {
                        RequestCallState<ActorLookupResult>
                            .CancelFromUserData(h);
                    }, handle));
            var rc = NativeMethods.zlink_remote_actor_get_ref(node.Handle,
                ref nativeNodeRid, actorId, LookupHandlerPtr,
                GCHandle.ToIntPtr(handle), timeoutMs);
            if (rc != 0)
            {
                handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            return completion.Task;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    internal static bool RemoteActorGetRefCallback(SpotNode node,
        RoutingId targetNodeRid, string actorId, TimeSpan timeout,
        ActorLookupHandler callback)
    {
        try
        {
            AttachTaskCallback(
                () => RemoteActorGetRefAsync(node, targetNodeRid, actorId,
                    timeout, CancellationToken.None),
                result => () => callback(result),
                error => () => callback(new ActorLookupResult(
                    MapRequestFailure(error), default, 0)),
                () => callback(new ActorLookupResult(RequestResult.Terminated,
                    default, 0)));
            return true;
        }
        catch (ZlinkException error) when (
            RequestReplySupport.MapSendNoWaitResult(error)
            == SendResult.Backpressured)
        {
            return false;
        }
    }
}
