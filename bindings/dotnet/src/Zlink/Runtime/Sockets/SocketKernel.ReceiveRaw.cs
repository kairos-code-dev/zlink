// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    private unsafe byte[][] ReceiveRawFramesCore(int flags)
    {
        return ReceiveRawFrameSequence(flags, includeRoutingFrames: true)
            .ToArray();
    }

    private unsafe int ReceiveRawFrameCore(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        List<byte[]> frames = ReceiveRawFrameSequence(flags,
            includeRoutingFrames: false);
        if (frames.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        return CopyFirstFrameAndCollectPending(frames, destination,
            out pendingFrames);
    }

    private unsafe int ReceiveRawRoutedFrameCore(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out int routingLength,
        out byte[][] pendingFrames)
    {
        List<byte[]> payloadFrames = ReceiveRawFrameSequence(flags,
            includeRoutingFrames: false, out byte[]? routingBytes,
            out _, out _);
        if (payloadFrames.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        routingLength = routingBytes == null
            ? 0
            : CopyRoutingId(routingBytes, routingDestination);
        return CopyFirstFrameAndCollectPending(payloadFrames, payloadDestination,
            out pendingFrames);
    }
}
