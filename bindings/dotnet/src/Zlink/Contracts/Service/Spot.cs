// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Invoked when a spot can accept more sends after back-pressure.
/// </summary>
public delegate void SpotSendReadyHandler();

/// <summary>
///     Invoked for each dispatch event surfaced by a spot.
/// </summary>
public delegate void SpotDispatchHandler(SpotDispatchInfo info);

/// <summary>
///     A spot: a multi-role messaging endpoint that can publish, subscribe, route,
///     request, reply, and host actors.
/// </summary>
public interface ISpot : IZlinkSocket, IDisposable, IAsyncDisposable
{
    /// <summary>
    ///     Gets the routing id that identifies this spot to its peers.
    /// </summary>
    RoutingId RoutingId { get; }

    /// <summary>
    ///     Gets or sets how long a request waits for a reply before timing out;
    ///     null waits indefinitely.
    /// </summary>
    TimeSpan? RequestTimeout { get; set; }

    /// <summary>
    ///     Gets or sets the maximum number of outbound messages queued before
    ///     back-pressure; 0 means no limit.
    /// </summary>
    int SendHighWaterMark { get; set; }

    /// <summary>
    ///     Gets or sets the maximum number of inbound messages queued before
    ///     back-pressure; 0 means no limit.
    /// </summary>
    int ReceiveHighWaterMark { get; set; }

    /// <summary>
    ///     Gets or sets the OS send buffer size in bytes; -1 keeps the OS default.
    /// </summary>
    int SendBufferSize { get; set; }

    /// <summary>
    ///     Gets or sets the OS receive buffer size in bytes; -1 keeps the OS default.
    /// </summary>
    int ReceiveBufferSize { get; set; }

    /// <summary>
    ///     Gets or sets how long a blocking send waits before failing; null blocks
    ///     indefinitely.
    /// </summary>
    TimeSpan? SendTimeout { get; set; }

    /// <summary>
    ///     Gets or sets how long a blocking receive waits before failing; null
    ///     blocks indefinitely.
    /// </summary>
    TimeSpan? ReceiveTimeout { get; set; }

    /// <summary>
    ///     Gets or sets how long close waits to deliver queued messages; null waits
    ///     indefinitely.
    /// </summary>
    TimeSpan? Linger { get; set; }

    /// <summary>
    ///     Sets the routing id that identifies this spot to its peers.
    /// </summary>
    void SetRoutingId(RoutingId routingId);

    /// <summary>
    ///     Begins publishing under <paramref name="topic" />; parts are consumed on
    ///     a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    SendOperation Publish(string topic);

    /// <summary>
    ///     Begins a send addressed to the channel <paramref name="channelName" />;
    ///     parts are consumed on a successful submit.
    /// </summary>
    SendOperation SendToChannel(string channelName);

    /// <summary>
    ///     Begins a request to the channel <paramref name="channelName" />; parts
    ///     are consumed on a successful submit and a reply is awaited.
    /// </summary>
    RequestOperation RequestToChannel(string channelName);

    /// <summary>
    ///     Begins a send addressed to a spot on another node; parts are consumed on
    ///     a successful submit.
    /// </summary>
    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);

    /// <summary>
    ///     Begins a request to a spot on another node; parts are consumed on a
    ///     successful submit and a reply is awaited.
    /// </summary>
    RequestOperation RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);

    /// <summary>
    ///     Begins a request to a ROUTER peer; parts are consumed on a successful
    ///     submit and a reply is awaited.
    /// </summary>
    RequestOperation RequestToRouter(RoutingId peerRid);

    /// <summary>
    ///     Begins a reply to the spot request identified by
    ///     <paramref name="requestSeq" />; parts are consumed on a successful submit.
    /// </summary>
    ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq);

    /// <summary>
    ///     Begins a reply to a ROUTER peer's request identified by
    ///     <paramref name="requestSeq" />; parts are consumed on a successful submit.
    /// </summary>
    ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq);

    /// <summary>
    ///     Adds a subscription for <paramref name="topicOrPattern" />; subscriptions
    ///     accumulate.
    /// </summary>
    void SetSubscription(string topicOrPattern);

    /// <summary>
    ///     Removes a subscription previously added for
    ///     <paramref name="topicOrPattern" />.
    /// </summary>
    void UnsetSubscription(string topicOrPattern);

    /// <summary>
    ///     Gets the active subscription at <paramref name="index" />, or null when
    ///     the index is out of range.
    /// </summary>
    SubscriptionEntry? SubscriptionAt(int index);

    /// <summary>
    ///     Receives the next matching topic message into caller-provided storage;
    ///     false when <see cref="RecvFlags.DontWait" /> is set and none is available.
    /// </summary>
    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);

    /// <summary>
    ///     Receives the next subscriber (un)subscription event into caller-provided
    ///     storage; false when <see cref="RecvFlags.DontWait" /> is set and none is
    ///     available.
    /// </summary>
    bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None);

    /// <summary>
    ///     Receives the next routed message into caller-provided storage; false
    ///     when <see cref="RecvFlags.DontWait" /> is set and none is available.
    /// </summary>
    bool RecvRouted(Received result, RecvFlags flags = RecvFlags.None);

    /// <summary>
    ///     Runs pending reply callbacks for requests sent directly from this spot.
    ///     Returns the number of callbacks that ran.
    /// </summary>
    int DrainReplies();

    /// <summary>
    ///     Receives the next pending actor-join request, or null when none is
    ///     available.
    /// </summary>
    ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None);

    /// <summary>
    ///     Receives the next actor lifecycle event, or null when none is available.
    /// </summary>
    SpotActorLifecycleEvent? RecvActorLifecycle(
        RecvFlags flags = RecvFlags.None);

    /// <summary>
    ///     Begins a reply to <paramref name="request" /> carrying
    ///     <paramref name="joinResultCode" />; parts are consumed on a successful
    ///     submit.
    /// </summary>
    ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request,
        int joinResultCode);

    /// <summary>
    ///     Returns the actors currently hosted on this spot. The caller owns the
    ///     returned array.
    /// </summary>
    ActorRef[] Actors();

    /// <summary>
    ///     Registers a callback invoked when the spot can accept more sends after
    ///     back-pressure. The callback runs on a background dispatch thread.
    /// </summary>
    void SetSendReadyHandler(SpotSendReadyHandler handler);

    /// <summary>
    ///     Registers the callback invoked for each spot dispatch event. The
    ///     callback runs on a background dispatch thread.
    /// </summary>
    void SetDispatchHandler(SpotDispatchHandler handler);

    /// <summary>
    ///     Closes the spot and releases its resources.
    /// </summary>
    void Close();
}