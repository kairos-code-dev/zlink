// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

internal static class EnumValidation
{
    private const SocketEvent ValidSocketEvents = SocketEvent.All;
    private const PollEvents ValidPollEvents = PollEvents.PollIn
        | PollEvents.PollOut
        | PollEvents.PollErr
        | PollEvents.PollPri;

    internal static void EnsureContextOption(ContextOption option,
        string paramName)
    {
        if (!Enum.IsDefined(option))
        {
            throw new ArgumentOutOfRangeException(paramName, option,
                "Unknown context option.");
        }
    }

    internal static void EnsureSocketEvents(SocketEvent events, string paramName)
    {
        if ((((ulong)events) & ~((ulong)ValidSocketEvents)) != 0)
        {
            throw new ArgumentOutOfRangeException(paramName, events,
                "Unknown socket monitor event flag.");
        }
    }

    internal static void EnsurePollEvents(PollEvents events, string paramName)
    {
        if ((((int)events) & ~((int)ValidPollEvents)) != 0)
        {
            throw new ArgumentOutOfRangeException(paramName, events,
                "Unknown poll event flag.");
        }
    }
}
