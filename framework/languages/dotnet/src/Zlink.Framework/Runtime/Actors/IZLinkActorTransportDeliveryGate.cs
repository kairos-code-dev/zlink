namespace Zlink.Framework.Runtime.Actors;

// A narrowly scoped internal seam used to delay a delivery after authority
// resolution but before the selected transport route is submitted. Production
// hosts do not register this service, so the normal path has no allocation or
// asynchronous dispatch cost beyond the nullable service lookup.
internal interface IZLinkActorTransportDeliveryGate
{
    ValueTask WaitAsync(
        ZLinkActorTransportDelivery delivery,
        CancellationToken cancellationToken);
}

internal readonly record struct ZLinkActorTransportDelivery(
    string OperationId,
    string ActorId,
    ZLinkActorTransportOperationKind Kind);

internal enum ZLinkActorTransportOperationKind
{
    OneWay = 0,
    Request = 1
}

internal static class ZLinkActorTransportDeliveryMetadata
{
    internal const string OperationId = "zlink.e2e.transport-operation-id";
}
