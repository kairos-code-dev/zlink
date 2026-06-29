namespace DeliveryDispatch.Shared.Contracts;

public enum DeliveryStatus
{
    Created,
    Assigned,
    Accepted,
    Reassigned,
    PickedUp,
    Delivered,
    Failed
}

public sealed record CreateDeliveryRequest(
    string DeliveryId,
    string CustomerId,
    string PickupAddress,
    string DropoffAddress);

public sealed record DeliveryCreated(
    string DeliveryId);

public sealed record EnsureCustomerActor(
    string CustomerId);

public sealed record CustomerActorEnsured(
    string CustomerId,
    ActorRefSnapshot Actor);

public sealed record BindCourier(
    string CourierId,
    string SessionRoute);

public sealed record CourierBound(
    string CourierId,
    ActorRefSnapshot Actor,
    string SessionRoute);

public sealed record BindCourierSession(
    string CourierId,
    ActorRefSnapshot? Actor = null,
    string? SessionRoute = null);

public sealed record EnsureCourierActor(
    string CourierId);

public sealed record CourierActorEnsured(
    string CourierId,
    ActorRefSnapshot Actor);

public sealed record SubscribeDelivery(
    string DeliveryId);

public sealed record SubscribeDeliveryAccepted(
    string DeliveryId);

public sealed record AssignDelivery(
    string DeliveryId,
    string CustomerId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDelivery(
    string CourierId,
    string DeliveryId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryResult(
    string DeliveryId,
    string CourierId,
    bool Accepted,
    string? Reason);

public sealed record CourierDecision(
    string DeliveryId,
    string CourierId,
    bool Accepted,
    string? Reason);

public sealed record ReassignDelivery(
    string DeliveryId,
    string PreviousCourierId,
    string NextCourierId,
    string Reason);

public sealed record DeliveryStatusChanged(
    string DeliveryId,
    DeliveryStatus Status,
    string? CourierId,
    DateTimeOffset OccurredAt);

public sealed record DeliveryStatusAck(
    string DeliveryId,
    DeliveryStatus Status);

public sealed record DeliveryStatusNotify(
    string DeliveryId,
    DeliveryStatus Status,
    string? CourierId,
    DateTimeOffset OccurredAt);

public sealed record ServerAssertionReq(
    string SuccessfulDeliveryId,
    string ReassignedDeliveryId);

public sealed record ServerAssertionRes(
    bool Passed,
    string[] Evidence);

public sealed record ActorRefSnapshot(
    string NodeRid,
    string ActorId,
    ulong Generation);
