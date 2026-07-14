namespace DeliveryDispatch.Shared.Contracts;

using Zlink.Framework.Contracts.Actors;

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
    string NodeRid,
    string SessionRoute)
{
    public CourierActorBindingSnapshot Actor => new(NodeRid);
}

public sealed record CourierActorBindingSnapshot(
    string NodeRid);

public sealed record BindCourierReq(
    string CourierId,
    string SessionRoute);

public sealed record BindCourierRes(
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

/// <summary>
/// The offer, and the courier's answer to it. Both are one-way (common sample spec §7.4): a
/// courier looks at a screen and presses a button, and tying that time to a request's reply
/// would hold the spot's serial queue open for as long as the courier thinks. The server cannot
/// request anything of a client anyway — it can only push — so the decision was always going to
/// arrive as a separate inbound message. <c>Attempt</c> is what makes the pairing safe: a
/// decision that names an attempt other than the current one arrived too late and is dropped.
/// </summary>
public sealed record OfferDeliveryMsg(
    string CourierId,
    string DeliveryId,
    int Attempt,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryNotify(
    string CourierId,
    string DeliveryId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryResultMsg(
    string DeliveryId,
    string CourierId,
    int Attempt,
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
