// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   dotnet run --project samples/ActorRoomExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        // --8<-- [start:doc]
        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateMeshNode();
        SampleSupport.StartConfiguredNode(node, "actor-room-example");
        using ISpot room = node.CreateSpot();
        ActorRef player1 = node.CreateActor("player-1");
        ActorRef player2 = node.CreateActor("player-2");
        using var stream = ctx.CreateStreamSocket();
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> received = new();

        // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
        RoutingId session = RoutingId.From("game-room-session");
        using var sessionService = SampleSupport.StartSessionService(node, stream);
        SampleSupport.BindSessionActor(node, sessionService, session, player1);
        SampleSupport.BindSessionActor(node, sessionService, session, player2);

        // 두 플레이어가 방에 합류한다 (호스트가 각각 admit).
        ulong epoch1 = SampleSupport.JoinLocalSpot(node, player1, room, "enter-room");
        ulong epoch2 = SampleSupport.JoinLocalSpot(node, player2, room, "enter-room");

        // 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
        void SendAndWait(ActorRef player, string text, int want)
        {
            SampleSupport.RelaySessionMessage(sessionService, session, player,
                text);
            SampleSupport.WaitOrThrow(() =>
            {
                SampleSupport.CollectActorMessages(node, ready, recv, received);
                return received.Count >= want;
            }, 5000, $"message to {player.ActorId}");
        }

        // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
        SendAndWait(player1, "your-turn", 1);
        SendAndWait(player2, "wait", 2);

        if (!received.SequenceEqual(new[] { "your-turn", "wait" }))
            throw new Exception($"messages were not routed per actor: {string.Join(",", received)}");

        SampleSupport.LeaveLocalSpot(node, player1, epoch1);
        SampleSupport.LeaveLocalSpot(node, player2, epoch2);
        Console.WriteLine("[actor/room] player-1: \"your-turn\", player-2: \"wait\"");
        // --8<-- [end:doc]
    }
}
