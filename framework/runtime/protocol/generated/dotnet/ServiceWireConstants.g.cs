// Generated from service-wire-v1.schema.json. Do not edit.
namespace Systems.Zlink.Framework.Runtime.Protocol;

internal static class ServiceWireConstants
{
    internal const byte Magic0 = 90;
    internal const byte Magic1 = 77;
    internal const byte WireMajor = 1;
    internal const string RequiredCapability = "framework-service-v11";
    internal enum Command : byte
    {
        Hello = 1,
        Admit = 2,
        Reject = 3,
        Update = 4,
        LivenessProbe = 5,
        LivenessAck = 6,
        NodeSend = 16,
        NodeRequest = 17,
        ChannelSend = 18,
        ChannelRequest = 19,
        Reply = 20,
        SpotSend = 21,
        SpotRequest = 22,
        LogicalMulticast = 23,
        ActorSend = 24,
        ActorRequest = 25,
        ActorLookup = 26,
        ActorDestroy = 27,
        ActorJoin = 28,
        ActorLeft = 29,
        TransferReady = 30,
        TransferData = 31,
        TransferAck = 32,
        ReplyRelay = 33,
        TransferSeal = 34,
        TransferComplete = 35,
        BoundSessionSend = 36,
        ActorJoined = 37,
        BoundSessionBind = 38,
        InstanceSpot = 39,
        TransferPrepare = 40,
        TransferReserved = 41,
        SessionTransferSeal = 42,
        SessionTransferSealed = 43,
        SessionTransferRoute = 44,
        SessionTransferRouted = 45,
        ReplyRelayAck = 46,
    }
    [System.Flags]
    internal enum Flag : byte
    {
        None = 0,
        Metadata = 1,
        BoundSession = 2,
        SourceSpotRid = 4,
        Extension = 8,
    }
}
