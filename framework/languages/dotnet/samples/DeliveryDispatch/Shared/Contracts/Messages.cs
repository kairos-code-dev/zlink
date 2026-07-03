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

public sealed record CreateDeliveryReq(
    string DeliveryId,
    string CustomerId,
    string PickupAddress,
    string DropoffAddress);

public sealed record CreateDeliveryRes(
    string DeliveryId);

public sealed record EnsureCustomerActorReq(
    string CustomerId);

public sealed record EnsureCustomerActorRes(
    string CustomerId,
    ActorRefSnapshot Actor);

public sealed record FindCustomerActorReq(
    string CustomerId);

public sealed record FindCustomerActorRes(
    string CustomerId,
    ActorRefSnapshot? Actor);

public sealed record BindCourierSessionReq(
    string CourierId,
    ActorRefSnapshot? Actor = null,
    string? SessionRoute = null);

public sealed record BindCourierSessionRes(
    string CourierId,
    ActorRefSnapshot Actor,
    string SessionRoute);

public sealed record EnsureCourierActorReq(
    string CourierId);

public sealed record EnsureCourierActorRes(
    string CourierId,
    ActorRefSnapshot Actor);

public sealed record FindCourierActorReq(
    string CourierId);

public sealed record FindCourierActorRes(
    string CourierId,
    ActorRefSnapshot? Actor);

public sealed record SubscribeDeliveryReq(
    string DeliveryId);

public sealed record SubscribeDeliveryRes(
    string DeliveryId);

public sealed record AssignDeliveryMsg(
    string DeliveryId,
    string CustomerId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryReq(
    string CourierId,
    string DeliveryId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryNotify(
    string CourierId,
    string DeliveryId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryRes(
    string DeliveryId,
    string CourierId,
    bool Accepted,
    string? Reason);

public sealed record CourierDecisionMsg(
    string DeliveryId,
    string CourierId,
    bool Accepted,
    string? Reason);

public sealed record DeliveryStatusChangedReq(
    string DeliveryId,
    string CustomerId,
    DeliveryStatus Status,
    string? CourierId,
    DateTimeOffset OccurredAt);

public sealed record DeliveryStatusChangedRes(
    string DeliveryId,
    DeliveryStatus Status);

public sealed record DeliveryStatusNotify(
    string DeliveryId,
    DeliveryStatus Status,
    string? CourierId,
    DateTimeOffset OccurredAt);

public sealed record DeliveryStatusUpdatedMsg(
    string DeliveryId,
    string CustomerId,
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
