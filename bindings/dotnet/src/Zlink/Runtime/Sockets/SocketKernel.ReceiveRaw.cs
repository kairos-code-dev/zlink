// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    private byte[][] ReceiveRawFramesCore(int flags)
    {
        return ReceiveRawFrameSequence(flags, true)
            .ToArray();
    }

    private int ReceiveRawFrameCore(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        var frames = ReceiveRawFrameSequence(flags,
            false);
        if (frames.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        return CopyFirstFrameAndCollectPending(frames, destination,
            out pendingFrames);
    }

    private int ReceiveRawRoutedFrameCore(Span<byte> routingDestination,
        Span<byte> payloadDestination, int flags, out int routingLength,
        out byte[][] pendingFrames)
    {
        var payloadFrames = ReceiveRawFrameSequence(flags,
            false, out var routingBytes,
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