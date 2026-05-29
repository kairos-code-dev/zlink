// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static readonly NativeMethods.ZlinkActorLookupHandlerDelegate
        LookupHandler = OnLookupReply;

    internal static readonly IntPtr LookupHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(LookupHandler);

    internal static readonly NativeMethods.ZlinkActorJoinHandlerDelegate
        FullJoinHandler = OnActorJoinFull;

    internal static readonly IntPtr FullJoinHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(FullJoinHandler);

    internal static void OnReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (result != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    (RequestResult)result));
                return;
            }

            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = Received.Create((RoutingId?)null, replyParts);
            if (!state.TrySetResult(received))
                RequestReplySupport.DisposeParts(replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }

    internal static void OnJoinReply(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        int result = resultPtr == IntPtr.Zero
            ? (int)RequestResult.InternalError
            : Marshal.PtrToStructure<ZlinkActorJoinResult>(resultPtr).Result;
        OnReply(result, parts, partCount, userData);
    }

    internal static void OnNoopReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        if (parts != IntPtr.Zero)
            NativeMethods.zlink_multipart_close(parts, partCount);
    }

    internal static void OnActorJoinEntrySpot(IntPtr resultPtr,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        ActorJoinEntrySpotCallState state =
            (ActorJoinEntrySpotCallState)handle.Target!;
        try
        {
            if (resultPtr == IntPtr.Zero)
            {
                state.Completion.TrySetResult(new ActorJoinEntrySpotResult(
                    RequestResult.InternalError, default, default, 0, 0));
                return;
            }

            ZlinkActorJoinEntrySpotResult native = Marshal.PtrToStructure
                <ZlinkActorJoinEntrySpotResult>(resultPtr);
            ActorRef returnedActor =
                (RequestResult)native.Result == RequestResult.Ok
                    ? FromNative(ref native.Actor)
                    : default;
            RoutingId targetNodeRid = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.TargetNodeRid));
            ActorJoinEntrySpotResult result = new(
                (RequestResult)native.Result, returnedActor, targetNodeRid,
                native.JoinEpoch, native.Flags);
            state.Completion.TrySetResult(result);
        }
        finally
        {
            state.Cleanup();
            handle.Free();
        }
    }

    internal static void OnActorJoinFull(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        ActorJoinCallState state = (ActorJoinCallState)handle.Target!;
        try
        {
            if (resultPtr == IntPtr.Zero)
            {
                ActorJoinResult fail = new(RequestResult.InternalError, 0,
                    default, default, 0, 0);
                state.Completion.TrySetResult(
                    new ActorJoinResultEnvelope(fail,
                        Array.Empty<Message>()));
                return;
            }
            ZlinkActorJoinResult native = Marshal.PtrToStructure
                <ZlinkActorJoinResult>(resultPtr);
            if ((RequestResult)native.Result != RequestResult.Ok)
            {
                ActorJoinResult fail = new((RequestResult)native.Result,
                    native.JoinResultCode, default, default, native.JoinEpoch,
                    native.Flags);
                state.Completion.TrySetResult(
                    new ActorJoinResultEnvelope(fail,
                        Array.Empty<Message>()));
                return;
            }

            ActorRef returnedActor = FromNative(ref native.Actor);
            RoutingId joinedSpot = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.JoinedSpotRid));
            ActorJoinResult result = new((RequestResult)native.Result,
                native.JoinResultCode, returnedActor, joinedSpot,
                native.JoinEpoch, native.Flags);
            Message[] replyParts = parts != IntPtr.Zero
                ? Message.FromNativeVector(parts, partCount)
                : Array.Empty<Message>();
            parts = IntPtr.Zero;
            partCount = 0;
            state.Completion.TrySetResult(
                new ActorJoinResultEnvelope(result, replyParts));
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            state.Cleanup();
            handle.Free();
        }
    }

    internal static void OnLookupReply(IntPtr resultPtr, IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        ActorLookupCallState state = (ActorLookupCallState)handle.Target!;
        try
        {
            ActorLookupResult r;
            if (resultPtr == IntPtr.Zero)
            {
                r = new ActorLookupResult(RequestResult.InternalError,
                    default, 0);
            }
            else
            {
                ZlinkActorLookupResult native = Marshal.PtrToStructure
                    <ZlinkActorLookupResult>(resultPtr);
                RequestResult result = (RequestResult)native.Result;
                ActorRef actor = default;
                if (result == RequestResult.Ok)
                {
                    ActorRef? parsedActor = FromOptionalNative(ref native.Actor);
                    if (parsedActor is null)
                    {
                        result = RequestResult.InternalError;
                    }
                    else
                    {
                        actor = parsedActor.Value;
                    }
                }

                r = new ActorLookupResult(result, actor, native.Flags);
            }
            state.Completion.TrySetResult(r);
        }
        finally
        {
            state.Cleanup();
            handle.Free();
        }
    }

    internal sealed class ActorJoinCallState
    {
        public TaskCompletionSource<ActorJoinResultEnvelope> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public ActorJoinCallState(
            TaskCompletionSource<ActorJoinResultEnvelope> completion)
        {
            Completion = completion;
        }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal sealed class ActorLookupCallState
    {
        public TaskCompletionSource<ActorLookupResult> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public ActorLookupCallState(
            TaskCompletionSource<ActorLookupResult> completion)
        {
            Completion = completion;
        }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal sealed class ActorJoinEntrySpotCallState
    {
        public TaskCompletionSource<ActorJoinEntrySpotResult> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public ActorJoinEntrySpotCallState(
            TaskCompletionSource<ActorJoinEntrySpotResult> completion)
        {
            Completion = completion;
        }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal readonly struct ActorJoinResultEnvelope
    {
        public ActorJoinResult Result { get; }
        public IReadOnlyList<Message> Parts { get; }

        public ActorJoinResultEnvelope(ActorJoinResult result,
            IReadOnlyList<Message> parts)
        {
            Result = result;
            Parts = parts;
        }
    }
}
