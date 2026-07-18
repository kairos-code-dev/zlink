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
        node.SetBind("tcp://127.0.0.1:*");
        node.Start();
        using ISpot room = node.CreateSpot();
        ActorRef player1 = node.CreateActor("player-1");
        ActorRef player2 = node.CreateActor("player-2");
        using var stream = ctx.CreateStreamSocket();
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> received = new();

        RoutingId session = RoutingId.From("game-room-session");
        Zlink.MultipartClose(await stream.BindActor(session, player1)
            .Timeout(TimeSpan.FromSeconds(2)).Async());
        Zlink.MultipartClose(await stream.BindActor(session, player2)
            .Timeout(TimeSpan.FromSeconds(2)).Async());

        // 두 플레이어가 방에 합류한다 (호스트가 각각 admit).
        ulong epoch1 = SampleSupport.JoinLocalSpot(node, player1, room, "enter-room");
        ulong epoch2 = SampleSupport.JoinLocalSpot(node, player2, room, "enter-room");

        // 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
        void SendAndWait(string actorId, string text, int want)
        {
            using Message m = Message.From(text);
            stream.SendBoundActor(session, actorId).Message(m).Submit();
            SampleSupport.WaitOrThrow(() =>
            {
                SampleSupport.CollectActorMessages(node, ready, recv, received);
                return received.Count >= want;
            }, 5000, $"message to {actorId}");
        }

        // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
        SendAndWait("player-1", "your-turn", 1);
        SendAndWait("player-2", "wait", 2);

        if (!received.SequenceEqual(new[] { "your-turn", "wait" }))
            throw new Exception($"messages were not routed per actor: {string.Join(",", received)}");

        SampleSupport.LeaveLocalSpot(node, player1, epoch1);
        SampleSupport.LeaveLocalSpot(node, player2, epoch2);
        Console.WriteLine("[actor/room] player-1: \"your-turn\", player-2: \"wait\"");
        // --8<-- [end:doc]
    }
}
