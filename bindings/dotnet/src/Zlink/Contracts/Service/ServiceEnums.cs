// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines registry option values.
/// </summary>
public enum RegistryOption
{
    /// <summary>
    /// Represents the Id value.
    /// </summary>
    Id = 0x3801,
    /// <summary>
    /// Represents the HeartbeatIntervalMs value.
    /// </summary>
    HeartbeatIntervalMs = 0x3802,
    /// <summary>
    /// Represents the HeartbeatTimeoutMs value.
    /// </summary>
    HeartbeatTimeoutMs = 0x3803,
    /// <summary>
    /// Represents the BroadcastIntervalMs value.
    /// </summary>
    BroadcastIntervalMs = 0x3804
}

/// <summary>
/// Defines spot dispatch event values.
/// </summary>
public enum SpotDispatchEvent
{
    /// <summary>
    /// Represents the SubscribeReadable value.
    /// </summary>
    SubscribeReadable = 1,
    /// <summary>
    /// Represents the RoutedReadable value.
    /// </summary>
    RoutedReadable = 2,
    /// <summary>
    /// Represents the TimerReadable value.
    /// </summary>
    TimerReadable = 3,
    /// <summary>
    /// Represents the ChannelReplyReadable value.
    /// </summary>
    ChannelReplyReadable = 4,
    /// <summary>
    /// Represents the ActorReadable value.
    /// </summary>
    ActorReadable = 5,
    /// <summary>
    /// Represents the ActorJoinReadable value.
    /// </summary>
    ActorJoinReadable = 6,
    /// <summary>
    /// Represents the ActorLifecycleReadable value.
    /// </summary>
    ActorLifecycleReadable = 7
}

/// <summary>
/// Defines spot dispatch subject kind values.
/// </summary>
public enum SpotDispatchSubjectKind
{
    /// <summary>
    /// Represents the Spot value.
    /// </summary>
    Spot = 1,
    /// <summary>
    /// Represents the Timer value.
    /// </summary>
    Timer = 2,
    /// <summary>
    /// Represents the ChannelDealer value.
    /// </summary>
    ChannelDealer = 3,
    /// <summary>
    /// Represents the Actor value.
    /// </summary>
    Actor = 4
}
