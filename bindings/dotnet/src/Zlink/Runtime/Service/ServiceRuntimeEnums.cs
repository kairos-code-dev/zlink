// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal enum RegistrySocketRole
{
    Pub = 1,
    Router = 2,
    PeerSub = 3
}

internal enum SpotNodeSocketRole
{
    Node = 0,
    Pub = 1,
    Sub = 2,
    Dealer = 3
}

internal enum SpotNodeOption
{
    RouterHwmProfile = 0x360E,
    RouterHwm = 0x360F,
    PubSubHwmProfile = 0x3610,
    PubSubHwm = 0x3611,
    DispatchWorkersMin = 0x3612,
    DispatchWorkersMax = 0x3613
}

internal enum SpotSocketRole
{
    Pub = 1,
    Sub = 2
}
