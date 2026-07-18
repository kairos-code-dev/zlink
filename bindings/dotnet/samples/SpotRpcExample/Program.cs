// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
//   dotnet run --project samples/SpotRpcExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        // --8<-- [start:doc]
        using var ctx = Zlink.CreateContext();
        using var serverNode = ctx.CreateMeshNode(new MeshNodeOptions { MeshName = "spot-rpc" });
        using var clientNode = ctx.CreateMeshNode(new MeshNodeOptions { MeshName = "spot-rpc" });
        string serverEndpoint = SampleSupport.NewEndpoint("tcp", "spot-rpc-server");
        string clientEndpoint = SampleSupport.NewEndpoint("tcp", "spot-rpc-client");
        serverNode.SetBind(serverEndpoint);
        clientNode.SetBind(clientEndpoint);
        serverNode.Start();
        clientNode.Start();
        serverNode.ConnectPeer(clientEndpoint);
        clientNode.ConnectPeer(serverEndpoint);

        using var server = serverNode.CreateSpot();
        using var client = clientNode.CreateSpot();
        SampleSupport.WaitSpotPeerConnected(serverNode);
        SampleSupport.WaitSpotPeerConnected(clientNode);

        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();

        // 클라이언트 Spot이 서버 Spot으로 요청을 제출한다 (완료는 pull dispatch로 회수).
        using (Message ping = Message.From("ping"))
        {
            client.RequestToSpot(serverNode.RoutingId, server.RoutingId,
                server.Status().LifecycleGeneration, new[] { ping },
                out MeshOperationId _, TimeSpan.FromSeconds(3));
        }

        string? reply = null;
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (reply == null && DateTime.UtcNow < deadline)
        {
            // 서버 Spot은 라우티드 요청 레코드를 받아 같은 평면으로 응답한다.
            SampleSupport.PumpReady(serverNode, ready, recv, (record, batch, index) =>
            {
                if (record.Kind != MeshRecordKind.SpotRequest)
                    return;
                Message[] request = batch.RetainMessage(index);
                using (Message pong = Message.From("pong"))
                    record.Reply(new[] { pong });
                Zlink.MultipartClose(request);
            });

            // 클라이언트는 완료 레코드에서 응답을 회수한다.
            SampleSupport.PumpReady(clientNode, ready, recv, (record, batch, index) =>
            {
                if (record.Kind != MeshRecordKind.Completion
                    || record.OperationKind != MeshOperationKind.SpotRequest)
                    return;
                if (record.TerminalResult == 0 && record.PartCount > 0)
                {
                    Message[] parts = batch.RetainMessage(index);
                    reply = parts[0].GetString();
                    Zlink.MultipartClose(parts);
                }
            });

            if (reply != null)
                break;
            Thread.Sleep(10);
        }
        if (reply == null)
            throw new Exception("spot rpc reply did not arrive");

        Console.WriteLine($"[spot/rpc] request \"ping\" -> reply \"{reply}\"");
        // --8<-- [end:doc]
    }
}
