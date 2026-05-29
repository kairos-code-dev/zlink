// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines registry option values.
/// </summary>
public enum RegistryOption
{
    /// <summary>
    /// Indicates the id registry option.
    /// </summary>
    Id = 0x3801,
    /// <summary>
    /// Indicates the heartbeat interval milliseconds registry option.
    /// </summary>
    HeartbeatIntervalMs = 0x3802,
    /// <summary>
    /// Indicates the heartbeat timeout milliseconds registry option.
    /// </summary>
    HeartbeatTimeoutMs = 0x3803,
    /// <summary>
    /// Indicates the broadcast interval milliseconds registry option.
    /// </summary>
    BroadcastIntervalMs = 0x3804
}

/// <summary>
/// Defines spot dispatch event values.
/// </summary>
public enum SpotDispatchEvent
{
    /// <summary>
    /// Indicates the subscribe readable spot dispatch event.
    /// </summary>
    SubscribeReadable = 1,
    /// <summary>
    /// Indicates the routed readable spot dispatch event.
    /// </summary>
    RoutedReadable = 2,
    /// <summary>
    /// Indicates the timer readable spot dispatch event.
    /// </summary>
    TimerReadable = 3,
    /// <summary>
    /// Indicates the channel reply readable spot dispatch event.
    /// </summary>
    ChannelReplyReadable = 4,
    /// <summary>
    /// Indicates the actor readable spot dispatch event.
    /// </summary>
    ActorReadable = 5,
    /// <summary>
    /// Indicates the actor join readable spot dispatch event.
    /// </summary>
    ActorJoinReadable = 6,
    /// <summary>
    /// Indicates the actor lifecycle readable spot dispatch event.
    /// </summary>
    ActorLifecycleReadable = 7
}

/// <summary>
/// Defines spot dispatch subject kind values.
/// </summary>
public enum SpotDispatchSubjectKind
{
    /// <summary>
    /// Indicates the spot spot dispatch subject kind.
    /// </summary>
    Spot = 1,
    /// <summary>
    /// Indicates the timer spot dispatch subject kind.
    /// </summary>
    Timer = 2,
    /// <summary>
    /// Indicates the channel dealer spot dispatch subject kind.
    /// </summary>
    ChannelDealer = 3,
    /// <summary>
    /// Indicates the actor spot dispatch subject kind.
    /// </summary>
    Actor = 4
}
