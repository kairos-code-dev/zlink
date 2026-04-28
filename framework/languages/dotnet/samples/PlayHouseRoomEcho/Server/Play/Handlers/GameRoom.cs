using Zlink;
using PlayHouseRoomEcho.Server.Actors;

namespace PlayHouseRoomEcho.Server.Play.Handlers;

sealed class GameRoom : ZLinkSpot
{
    public GameRoom(RoutingId spotRid, RoutingId nodeRid)
        : base(spotRid, nodeRid)
    {
        AddActorJoin<GameRoomJoinHandler, PlayerActor, JoinRoomRequest, JoinRoomReply>();
    }
}
