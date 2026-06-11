// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
//   dotnet run --project samples/ActorSequentialExample
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        // --8<-- [start:doc]
        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var room = node.CreateSpot();
        // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
        using var player = node.CreateActor("player");
        using var stream = ctx.CreateStreamSocket();
        List<string> processed = new();

        stream.AttachActorGateway(node);
        RoutingId session = RoutingId.From("player-session");
        // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
        Zlink.MultipartClose(await stream.BindActor(session, player.Ref)
            .Timeout(TimeSpan.FromSeconds(2)).Async());

        // dispatch 핸들러: STREAM이 relay한 메시지를 모은다.
        room.SetDispatchHandler(info =>
        {
            ActorReceived? part;
            while ((part = info.RecvActor()) != null)
                using (part)
                    processed.Add(part.Message.GetString());
        });

        // join 요청을 수신해 "accepted"로 응답한다.
        void Accept()
        {
            for (int i = 0; i < 200; i++)
            {
                ActorJoinRequest? request = room.RecvActorJoin(RecvFlags.DontWait);
                if (request != null)
                {
                    using Message reply = Message.From("accepted");
                    room.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
                    return;
                }
                Thread.Sleep(10);
            }
            throw new TimeoutException("actor join request");
        }

        // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
        using (Message joinMsg = Message.From("enter-room"))
        {
            var task = player.Join(room).Message(joinMsg).Timeout(TimeSpan.FromSeconds(2)).Async();
            Accept();
            Zlink.MultipartClose((await task).Parts);
        }

        // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
        string[] commands = { "move", "attack", "loot" };
        foreach (string command in commands)
        {
            using Message m = Message.From(command);
            stream.SendBoundActor(session, "player").Message(m).Submit();
        }

        for (int i = 0; i < 200 && processed.Count < commands.Length; i++)
            Thread.Sleep(10);
        if (!processed.SequenceEqual(commands))
            throw new Exception($"messages were not processed in order: {string.Join(",", processed)}");

        Zlink.MultipartClose(await player.Leave(room).Timeout(TimeSpan.FromSeconds(2)).Async());
        Console.WriteLine("[actor/sequential] processed in order: move -> attack -> loot");
        // --8<-- [end:doc]
    }
}
