// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed class SocketCallbackRegistry
{
    internal NativeMethods.ZlinkStreamOnRawDelegate? StreamRawNative;
    internal NativeMethods.ZlinkStreamOnPacketDelegate? StreamPacketNative;
    internal StreamRawPacketHandler? StreamPacketHandler;
    internal StreamUInt32PacketHandler? StreamUInt32PacketHandler;
    internal StreamFramedPacketHandler? StreamFramedPacketHandler;
    internal StreamUInt32FramedPacketHandler? StreamUInt32FramedPacketHandler;
    internal SocketRecvHandler? RecvHandler;
    internal SocketSubscribeHandler? SubscribeHandler;
    internal Action? SendReadyHandler;
    internal SynchronizationContext? StreamRawContext;
    internal SynchronizationContext? StreamPacketContext;
    internal SynchronizationContext? RecvHandlerContext;
    internal SynchronizationContext? SubscribeHandlerContext;
    internal SynchronizationContext? SendReadyHandlerContext;
    internal NativeMethods.ZlinkSocketMsgHandlerDelegate? RecvHandlerNative;
    internal NativeMethods.ZlinkSubscribeHandlerDelegate? SubscribeHandlerNative;
    internal NativeMethods.ZlinkSendReadyHandlerDelegate? SendReadyHandlerNative;

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
