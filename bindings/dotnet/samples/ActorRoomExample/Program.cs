// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   dotnet run --project samples/ActorRoomExample
using Systems.Zlink;

using var ctx = Zlink.CreateContext();
using var node = ctx.CreateSpotNode();
using var room = node.CreateSpot();
using var player1 = node.CreateActor("player-1");
using var player2 = node.CreateActor("player-2");
using var stream = ctx.CreateStreamSocket();
List<string> received = new();

stream.AttachActorGateway(node);
RoutingId session = RoutingId.From("game-room-session");
Zlink.MultipartClose(await stream.BindActor(session, player1.Ref)
    .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
Zlink.MultipartClose(await stream.BindActor(session, player2.Ref)
    .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());

// dispatch 핸들러: 도착한 메시지를 모은다.
room.SetDispatchHandler(info =>
{
    ActorReceived? part;
    while ((part = info.RecvActor()) != null)
        using (part)
            received.Add(part.Message.GetString());
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

async Task Join(IActor actor)
{
    using Message m = Message.From("enter-room");
    var task = actor.Join(room).Message(m).Timeout(TimeSpan.FromSeconds(2)).SubmitAsync();
    Accept();
    Zlink.MultipartClose((await task).Parts);
}

// 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
void SendAndWait(string actorId, string text, int want)
{
    using Message m = Message.From(text);
    stream.SendBoundActor(session, actorId).Message(m).Submit();
    for (int i = 0; i < 200 && received.Count < want; i++)
        Thread.Sleep(10);
    if (received.Count < want)
        throw new Exception($"message to {actorId} not delivered");
}

await Join(player1);
await Join(player2);

// 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
SendAndWait("player-1", "your-turn", 1);
SendAndWait("player-2", "wait", 2);

if (!received.SequenceEqual(new[] { "your-turn", "wait" }))
    throw new Exception($"messages were not routed per actor: {string.Join(",", received)}");

Zlink.MultipartClose(await player1.Leave(room).Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
Zlink.MultipartClose(await player2.Leave(room).Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
Console.WriteLine("[actor/room] player-1: \"your-turn\", player-2: \"wait\"");
