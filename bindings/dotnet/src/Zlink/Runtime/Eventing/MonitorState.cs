// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

[Flags]
internal enum MonitorState
{
    None = 0,
    Ready = 1 << 0,
    BoundReady = 1 << 1,
    Closed = 1 << 3
}

[Flags]
internal enum MonitorStatusDetail
{
    None = 0,
    SendPendingMessages = 1 << 1,
    ReceivePendingMessages = 1 << 2
}
