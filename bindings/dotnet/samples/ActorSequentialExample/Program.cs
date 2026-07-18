// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 pull dispatch에서 들어온 순서대로 처리된다.
//   dotnet run --project samples/ActorSequentialExample
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
        using var room = node.CreateSpot();
        // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
        ActorRef player = node.CreateActor("player");
        using var stream = ctx.CreateStreamSocket();
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> processed = new();

        RoutingId session = RoutingId.From("player-session");
        // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
        Zlink.MultipartClose(await stream.BindActor(session, player)
            .Timeout(TimeSpan.FromSeconds(2)).Async());

        // join으로 Entry Spot에서 room(user Spot)으로 이동한다 (호스트가 admit).
        ulong epoch = SampleSupport.JoinLocalSpot(node, player, room, "enter-room");

        // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
        string[] commands = { "move", "attack", "loot" };
        foreach (string command in commands)
        {
            using Message m = Message.From(command);
            stream.SendBoundActor(session, "player").Message(m).Submit();
        }

        SampleSupport.WaitOrThrow(() =>
        {
            SampleSupport.CollectActorMessages(node, ready, recv, processed);
            return processed.Count >= commands.Length;
        }, 5000, "sequential actor messages");
        if (!processed.SequenceEqual(commands))
            throw new Exception($"messages were not processed in order: {string.Join(",", processed)}");

        SampleSupport.LeaveLocalSpot(node, player, epoch);
        Console.WriteLine("[actor/sequential] processed in order: move -> attack -> loot");
        // --8<-- [end:doc]
    }
}
