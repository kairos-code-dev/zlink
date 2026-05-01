using Zlink;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal readonly record struct ZLinkPlayRoute(
    string RouterChannelId,
    RoutingId PlayRouterId);
