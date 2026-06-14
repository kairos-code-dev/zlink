// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static Task<ActorLookupResult> RemoteActorGetRefAsync(
        SpotNode node, RoutingId targetNodeRid, string actorId,
        TimeSpan timeout, CancellationToken ct)
    {
        ValidateActorId(actorId, nameof(actorId));
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkRoutingId nativeNodeRid = targetNodeRid.ToNative();
        TaskCompletionSource<ActorLookupResult> completion = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            ActorLookupCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.CancelReg = ct.Register(static h =>
                {
                    GCHandle gh = (GCHandle)h!;
                    if (gh.Target is ActorLookupCallState s)
                    {
                        if (s.Completion.TrySetCanceled())
                            s.Cleanup();
                    }
                }, handle);
            }
            int rc = NativeMethods.zlink_remote_actor_get_ref(node.Handle,
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
        SynchronizationContext? syncCtx = SynchronizationContext.Current;
        try
        {
            _ = RemoteActorGetRefAsync(node, targetNodeRid, actorId, timeout,
                CancellationToken.None).ContinueWith(t =>
            {
                ActorLookupResult r;
                if (t.IsFaulted)
                {
                    Exception err = t.Exception!.GetBaseException();
                    RequestResult rr = err is ZlinkRequestException re
                        ? (RequestResult)re.Code
                        : RequestResult.InternalError;
                    r = new ActorLookupResult(rr, default, 0);
                }
                else if (t.IsCanceled)
                {
                    r = new ActorLookupResult(RequestResult.Terminated,
                        default, 0);
                }
                else
                {
                    r = t.Result;
                }
                CallbackDelivery.Post(syncCtx, () => callback(r));
            }, TaskScheduler.Default);
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
