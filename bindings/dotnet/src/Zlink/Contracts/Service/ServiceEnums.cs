// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// The kind of readable event surfaced by a spot dispatch.
/// </summary>
public enum SpotDispatchEvent
{
    /// <summary>
    /// A subscription has a message ready to read.
    /// </summary>
    SubscribeReadable = 1,
    /// <summary>
    /// A routed message is ready to read.
    /// </summary>
    RoutedReadable = 2,
    /// <summary>
    /// A timer has expired.
    /// </summary>
    TimerReadable = 3,
    /// <summary>
    /// A channel reply is ready to read.
    /// </summary>
    ChannelReplyReadable = 4,
    /// <summary>
    /// An actor message is ready to read.
    /// </summary>
    ActorReadable = 5,
    /// <summary>
    /// An actor join request is ready to read.
    /// </summary>
    ActorJoinReadable = 6,
    /// <summary>
    /// An actor lifecycle event is ready to read.
    /// </summary>
    ActorLifecycleReadable = 7
}

/// <summary>
/// What kind of subject a spot dispatch event concerns.
/// </summary>
public enum SpotDispatchSubjectKind
{
    /// <summary>
    /// The spot itself.
    /// </summary>
    Spot = 1,
    /// <summary>
    /// A timer.
    /// </summary>
    Timer = 2,
    /// <summary>
    /// A channel dealer socket.
    /// </summary>
    ChannelDealer = 3,
    /// <summary>
    /// An actor.
    /// </summary>
    Actor = 4
}
