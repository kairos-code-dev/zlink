// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

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
        RequestReplySupport.CompleteReceivedReply(result, parts, partCount,
            userData);
    }

    internal static void OnJoinReply(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        var result = resultPtr == IntPtr.Zero
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

    internal static void OnActorJoinEntrySpot(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        var state =
            (ActorJoinEntrySpotCallState)handle.Target!;
        try
        {
            if (resultPtr == IntPtr.Zero)
            {
                ActorJoinEntrySpotResult fail = new(
                    RequestResult.InternalError, 0, default, default, default,
                    0, 0);
                state.Completion.TrySetResult(
                    new ActorJoinEntrySpotResultEnvelope(fail,
                        Array.Empty<Message>()));
                return;
            }

            var native = Marshal.PtrToStructure
                <ZlinkActorJoinEntrySpotResult>(resultPtr);
            if ((RequestResult)native.Result != RequestResult.Ok)
            {
                ActorJoinEntrySpotResult fail = new((RequestResult)native.Result,
                    native.JoinResultCode, default, default, default,
                    native.JoinEpoch, native.Flags);
                state.Completion.TrySetResult(
                    new ActorJoinEntrySpotResultEnvelope(fail,
                        Array.Empty<Message>()));
                return;
            }

            var returnedActor = FromNative(ref native.Actor);
            var targetNodeRid = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.TargetNodeRid));
            var joinedSpotRid = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.JoinedSpotRid));
            ActorJoinEntrySpotResult result = new(
                (RequestResult)native.Result, native.JoinResultCode,
                returnedActor, targetNodeRid, joinedSpotRid, native.JoinEpoch,
                native.Flags);
            var replyParts = parts != IntPtr.Zero
                ? Message.FromNativeVector(parts, partCount)
                : Array.Empty<Message>();
            parts = IntPtr.Zero;
            partCount = 0;
            state.Completion.TrySetResult(
                new ActorJoinEntrySpotResultEnvelope(result, replyParts));
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            state.Cleanup();
            handle.Free();
        }
    }

    internal static void OnActorJoinFull(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        var state = (ActorJoinCallState)handle.Target!;
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

            var native = Marshal.PtrToStructure
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

            var returnedActor = FromNative(ref native.Actor);
            var joinedSpot = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.JoinedSpotRid));
            ActorJoinResult result = new((RequestResult)native.Result,
                native.JoinResultCode, returnedActor, joinedSpot,
                native.JoinEpoch, native.Flags);
            var replyParts = parts != IntPtr.Zero
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
        var handle = GCHandle.FromIntPtr(userData);
        var state = (ActorLookupCallState)handle.Target!;
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
                var native = Marshal.PtrToStructure
                    <ZlinkActorLookupResult>(resultPtr);
                var result = (RequestResult)native.Result;
                ActorRef actor = default;
                if (result == RequestResult.Ok)
                {
                    var parsedActor = FromOptionalNative(ref native.Actor);
                    if (parsedActor is null)
                        result = RequestResult.InternalError;
                    else
                        actor = parsedActor.Value;
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
        public ActorJoinCallState(
            TaskCompletionSource<ActorJoinResultEnvelope> completion)
        {
            Completion = completion;
        }

        public TaskCompletionSource<ActorJoinResultEnvelope> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal sealed class ActorLookupCallState
    {
        public ActorLookupCallState(
            TaskCompletionSource<ActorLookupResult> completion)
        {
            Completion = completion;
        }

        public TaskCompletionSource<ActorLookupResult> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal sealed class ActorJoinEntrySpotCallState
    {
        public ActorJoinEntrySpotCallState(
            TaskCompletionSource<ActorJoinEntrySpotResultEnvelope> completion)
        {
            Completion = completion;
        }

        public TaskCompletionSource<ActorJoinEntrySpotResultEnvelope> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

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

    internal readonly struct ActorJoinEntrySpotResultEnvelope
    {
        public ActorJoinEntrySpotResult Result { get; }
        public IReadOnlyList<Message> Parts { get; }

        public ActorJoinEntrySpotResultEnvelope(
            ActorJoinEntrySpotResult result, IReadOnlyList<Message> parts)
        {
            Result = result;
            Parts = parts;
        }
    }
}