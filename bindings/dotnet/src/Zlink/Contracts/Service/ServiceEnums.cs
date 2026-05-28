// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public enum RegistryOption
{
    Id = 0x3801,
    HeartbeatIntervalMs = 0x3802,
    HeartbeatTimeoutMs = 0x3803,
    BroadcastIntervalMs = 0x3804
}

public enum SpotDispatchEvent
{
    SubscribeReadable = 1,
    RoutedReadable = 2,
    TimerReadable = 3,
    ChannelReplyReadable = 4,
    ActorReadable = 5,
    ActorJoinReadable = 6,
    ActorLifecycleReadable = 7
}

public enum SpotDispatchSubjectKind
{
    Spot = 1,
    Timer = 2,
    ChannelDealer = 3,
    Actor = 4
}
