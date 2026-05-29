// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections;
using System.Collections.Generic;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

public sealed partial class Received : IDisposable
{
    internal void ReplyCore(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (_metadata is not { RequestSeq: { } requestSeq,
                ReplyHandler: { } replyHandler })
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        replyHandler(parts, flags);
    }

    internal bool SendCore(Message part, SendFlags flags = SendFlags.None)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        if (_sendKernel != null)
        {
            return _sendKernel.SendReceivedSingle(_sendRoutingIdSnapshot,
                _sendSpotRidSnapshot, part, flags);
        }
        if (_sendSingleHandler != null)
            return _sendSingleHandler(part, flags);
        return SendCore(new SingleMessageList(part), flags);
    }

    internal bool SendCore(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 1)
            return SendCore(parts[0], flags);
        if (_sendKernel != null)
        {
            return _sendKernel.SendReceivedParts(_sendRoutingIdSnapshot,
                _sendSpotRidSnapshot, parts, flags);
        }
        if (_sendHandler == null)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        return _sendHandler(parts, flags);
    }
}
