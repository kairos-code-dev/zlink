// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed class SocketCallbackRegistry
{
    internal SocketRecvHandler? RecvHandler;
    internal SynchronizationContext? RecvHandlerContext;
    internal NativeMethods.ZlinkSocketMsgHandlerDelegate? RecvHandlerNative;
    internal Action? SendReadyHandler;
    internal SynchronizationContext? SendReadyHandlerContext;
    internal NativeMethods.ZlinkSendReadyHandlerDelegate? SendReadyHandlerNative;
    internal StreamFramedPacketHandler? StreamFramedPacketHandler;
    internal SynchronizationContext? StreamPacketContext;
    internal StreamRawPacketHandler? StreamPacketHandler;
    internal NativeMethods.ZlinkStreamOnPacketDelegate? StreamPacketNative;
    internal SynchronizationContext? StreamRawContext;
    internal NativeMethods.ZlinkStreamOnRawDelegate? StreamRawNative;
    internal StreamUInt32FramedPacketHandler? StreamUInt32FramedPacketHandler;
    internal StreamUInt32PacketHandler? StreamUInt32PacketHandler;
    internal SocketSubscribeHandler? SubscribeHandler;
    internal SynchronizationContext? SubscribeHandlerContext;
    internal NativeMethods.ZlinkSubscribeHandlerDelegate? SubscribeHandlerNative;

    internal void ClearStream()
    {
        StreamPacketHandler = null;
        StreamUInt32PacketHandler = null;
        StreamFramedPacketHandler = null;
        StreamUInt32FramedPacketHandler = null;
        StreamRawNative = null;
        StreamPacketNative = null;
        StreamRawContext = null;
        StreamPacketContext = null;
    }

    internal void ClearReceive()
    {
        RecvHandler = null;
        RecvHandlerContext = null;
        RecvHandlerNative = null;
    }

    internal void ClearSubscribe()
    {
        SubscribeHandler = null;
        SubscribeHandlerContext = null;
        SubscribeHandlerNative = null;
    }

    internal void ClearSendReady()
    {
        SendReadyHandler = null;
        SendReadyHandlerContext = null;
        SendReadyHandlerNative = null;
    }

    internal void ClearAllNonStream()
    {
        ClearReceive();
        ClearSubscribe();
        ClearSendReady();
    }
}