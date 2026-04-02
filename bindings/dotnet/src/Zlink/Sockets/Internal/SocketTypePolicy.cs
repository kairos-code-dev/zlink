// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink.Sockets.Internal;

internal sealed class SocketTypePolicy
{
    private readonly SocketType _socketType;

    public SocketTypePolicy(SocketType socketType)
    {
        _socketType = socketType;
    }

    public SocketType SocketType => _socketType;

    public void EnsureSupportsMember(string memberName, SocketCapability capability)
    {
        if (Supports(capability))
            return;

        throw new InvalidOperationException(
            $"Socket type '{_socketType}' does not support '{memberName}'.");
    }

    public void EnsureOptionSupported(SocketOption option)
    {
        if (Supports(option))
            return;

        throw new InvalidOperationException(
            $"Socket option '{option}' is not supported by socket type '{_socketType}'.");
    }

    private bool Supports(SocketCapability capability)
    {
        return capability switch
        {
            SocketCapability.MessageSend => _socketType == SocketType.Pair
                || _socketType == SocketType.Dealer,
            SocketCapability.MessageReceive => _socketType == SocketType.Pair
                || _socketType == SocketType.Dealer,
            SocketCapability.RoutedSend => _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            SocketCapability.RoutedReceive => _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            SocketCapability.Publish => _socketType == SocketType.Pub
                || _socketType == SocketType.XPub,
            SocketCapability.SubscriptionControl => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketCapability.SubscribeReceive => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketCapability.ReceiveHandler => _socketType == SocketType.Pair
                || _socketType == SocketType.Dealer
                || _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            SocketCapability.SubscribeHandler => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketCapability.SubscriptionEvents => _socketType == SocketType.XPub,
            SocketCapability.StreamAttach => _socketType == SocketType.Stream,
            _ => false
        };
    }

    private bool Supports(SocketOption option)
    {
        return option switch
        {
            SocketOption.Subscribe or SocketOption.Unsubscribe
                or SocketOption.OnlyFirstSubscribe => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketOption.StreamNotify => _socketType == SocketType.Stream,
            SocketOption.XPubVerbose or SocketOption.XPubVerboser
                or SocketOption.XPubManual or SocketOption.XPubManualLastValue
                or SocketOption.XPubWelcomeMsg or SocketOption.TopicsCount
                => _socketType == SocketType.Pub || _socketType == SocketType.XPub,
            SocketOption.SubTopicsCount => _socketType == SocketType.Sub
                || _socketType == SocketType.XSub,
            SocketOption.XPubNoDrop => _socketType == SocketType.Pub
                || _socketType == SocketType.XPub,
            SocketOption.RouterMandatory or SocketOption.RouterHandover
                => _socketType == SocketType.Router,
            SocketOption.ProbeRouter => _socketType == SocketType.Dealer
                || _socketType == SocketType.Router,
            SocketOption.RoutingId => _socketType == SocketType.Dealer
                || _socketType == SocketType.Router,
            SocketOption.ConnectRoutingId => _socketType == SocketType.Router
                || _socketType == SocketType.Stream,
            _ => true
        };
    }

    internal enum SocketCapability
    {
        MessageSend,
        MessageReceive,
        RoutedSend,
        RoutedReceive,
        Publish,
        SubscriptionControl,
        SubscribeReceive,
        ReceiveHandler,
        SubscribeHandler,
        SubscriptionEvents,
        StreamAttach
    }
}
