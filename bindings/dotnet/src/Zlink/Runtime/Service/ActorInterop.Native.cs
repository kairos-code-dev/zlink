// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static void ValidateActorId(string actorId, string paramName)
    {
        if (actorId == null)
            throw new ArgumentNullException(paramName);
        if (actorId.IndexOf('\0') >= 0)
            throw new ArgumentException("Actor id must not contain NUL.",
                paramName);
        BoundaryValidation.ValidateFixedUtf8(actorId, paramName);
    }

    internal static unsafe ActorRef FromNative(ref ZlinkActorRef native)
    {
        fixed (byte* actorId = native.ActorId)
        {
            var nodeRid = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.NodeRid));
            return new ActorRef(nodeRid,
                NativeHelpers.ReadFixedString(actorId,
                    NativeConstants.ActorIdMax),
                native.Generation);
        }
    }

    internal static unsafe ActorRef? FromOptionalNative(ref ZlinkActorRef native)
    {
        var nodeRid = RoutingIdInterop.FromNative(ref native.NodeRid);
        if (nodeRid == null)
            return null;

        fixed (byte* actorId = native.ActorId)
        {
            var id = NativeHelpers.ReadFixedString(actorId,
                NativeConstants.ActorIdMax);
            if (id.Length == 0 && native.Generation == 0)
                return null;
            return new ActorRef(nodeRid.Value, id, native.Generation);
        }
    }

    internal static unsafe ZlinkActorRef ToNative(ActorRef actor)
    {
        ZlinkActorRef native = default;
        if (!actor.NodeRid.IsEmpty)
            native.NodeRid = actor.NodeRid.ToNative();
        var actorId = native.ActorId;
        NativeHelpers.WriteFixedString(actor.ActorId, actorId,
            NativeConstants.ActorIdMax);
        native.Generation = actor.Generation;
        return native;
    }

    internal static ActorRecvInfo FromNative(ref ZlinkActorRecvInfo native)
    {
        return new ActorRecvInfo(
            FromNative(ref native.Actor),
            RoutingIdInterop.FromNative(ref native.SourceNodeRid)
            ?? throw new ZlinkRecvException(
                ZlinkRecvException.ErrorCode.InternalError),
            RoutingIdInterop.FromNative(ref native.SourceSessionRid)
            ?? throw new ZlinkRecvException(
                ZlinkRecvException.ErrorCode.InternalError),
            native.RequestId,
            native.Flags);
    }

    internal static ActorJoinInfo FromNative(ref ZlinkActorJoinInfo native)
    {
        return new ActorJoinInfo(
            FromNative(ref native.SourceActor),
            FromNative(ref native.TargetActor),
            RoutingIdInterop.FromNative(ref native.SourceNodeRid)
            ?? throw new ZlinkRecvException(
                ZlinkRecvException.ErrorCode.InternalError),
            RoutingIdInterop.FromNative(ref native.SourceSpotRid)
            ?? throw new ZlinkRecvException(
                ZlinkRecvException.ErrorCode.InternalError),
            RoutingIdInterop.FromNative(ref native.TargetNodeRid)
            ?? throw new ZlinkRecvException(
                ZlinkRecvException.ErrorCode.InternalError),
            RoutingIdInterop.FromNative(ref native.TargetSpotRid)
            ?? throw new ZlinkRecvException(
                ZlinkRecvException.ErrorCode.InternalError),
            native.JoinEpoch,
            native.Flags);
    }

    internal static ActorRoute FromNative(ref ZlinkActorRoute native)
    {
        return new ActorRoute(
            FromNative(ref native.Actor),
            RoutingIdInterop.FromNative(ref native.CurrentSpotRid)
            ?? throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InternalError),
            (SpotKind)native.CurrentSpotKind);
    }

    internal static SpotActorLifecycleInfo FromNative(
        ref ZlinkSpotActorLifecycleInfo native)
    {
        return new SpotActorLifecycleInfo(
            FromOptionalNative(ref native.PreviousActor) ?? default,
            FromOptionalNative(ref native.CurrentActor) ?? default,
            RoutingIdInterop.FromNative(ref native.PreviousSpotRid),
            RoutingIdInterop.FromNative(ref native.CurrentSpotRid),
            native.JoinEpoch,
            native.Flags);
    }

    internal static uint NormalizeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }
}
