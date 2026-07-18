// 자립형 가이드 예제: 채널 요청/응답을 async로 대기.
// (10.0.0에서 route bridge는 제거됐고, 채널 요청은 MeshNode 위에서 pull dispatch로
//  완료를 회수한다. 여기선 그 완료 회수를 Task로 감싸 async await 형태를 보인다.)
//   dotnet run --project samples/SpotRequestAsync
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        if (!SampleSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var requesterNode = ctx.CreateMeshNode(new MeshNodeOptions { MeshName = "orders-mesh" });
        using var responderNode = ctx.CreateMeshNode(new MeshNodeOptions { MeshName = "orders-mesh" });
        const string channelName = "orders";
        string requesterEndpoint = SampleSupport.NewEndpoint("tcp", "spot-request-async-req");
        string responderEndpoint = SampleSupport.NewEndpoint("tcp", "spot-request-async-res");
        requesterNode.AddChannel(channelName);
        responderNode.AddChannel(channelName);
        requesterNode.SetBind(requesterEndpoint);
        responderNode.SetBind(responderEndpoint);
        requesterNode.Start();
        responderNode.Start();
        requesterNode.ConnectPeer(responderEndpoint);
        responderNode.ConnectPeer(requesterEndpoint);
        SampleSupport.WaitSpotPeerConnected(requesterNode);
        SampleSupport.WaitSpotPeerConnected(responderNode);

        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

        // 응답자: 채널 요청 레코드를 받아 "spot-pong"으로 응답한다.
        Task responderTask = Task.Run(() =>
        {
            using var ready = new MeshReadyBatch();
            using var recv = new MeshReceiveBatch();
            bool answered = false;
            while (!answered && !cts.IsCancellationRequested)
            {
                SampleSupport.PumpReady(responderNode, ready, recv, (record, batch, index) =>
                {
                    if (record.Kind != MeshRecordKind.ChannelRequest)
                        return;
                    Message[] request = batch.RetainMessage(index);
                    SampleSupport.EnsureEqual("spot-ping", request[0].GetString(), "request");
                    using (Message reply = Message.From("spot-pong"))
                        record.Reply(new[] { reply });
                    Zlink.MultipartClose(request);
                    answered = true;
                });
                Thread.Sleep(10);
            }
        });

        // 요청자: 채널로 요청을 제출하고 완료 레코드를 비동기로 기다린다.
        var completion = new TaskCompletionSource<string>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        Task requesterTask = Task.Run(() =>
        {
            using var ready = new MeshReadyBatch();
            using var recv = new MeshReceiveBatch();
            using (Message request = Message.From("spot-ping"))
            {
                requesterNode.RequestToChannel(channelName, new[] { request },
                    out MeshOperationId _, TimeSpan.FromSeconds(3));
            }
            while (!completion.Task.IsCompleted && !cts.IsCancellationRequested)
            {
                SampleSupport.PumpReady(requesterNode, ready, recv, (record, batch, index) =>
                {
                    if (record.Kind != MeshRecordKind.Completion
                        || record.OperationKind != MeshOperationKind.ChannelRequest)
                        return;
                    if (record.TerminalResult == 0 && record.PartCount > 0)
                    {
                        Message[] parts = batch.RetainMessage(index);
                        completion.TrySetResult(parts[0].GetString());
                        Zlink.MultipartClose(parts);
                    }
                });
                Thread.Sleep(10);
            }
        });

        string reply = await completion.Task.WaitAsync(TimeSpan.FromSeconds(5));
        SampleSupport.EnsureEqual("spot-pong", reply, "reply");
        await responderTask;
        await requesterTask;
        Console.WriteLine(
            "[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
    }
}
