/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   bindings/java/gradlew -p . :samples:runSpotChannelExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotRouteBridge;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.List;

public final class SpotChannelExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             SpotNode roomNode = ctx.createSpotNode();
             Spot room = roomNode.createSpot();
             DealerSocket roomDealer = ctx.createDealerSocket();
             RouterSocket apiRouter = ctx.createRouterSocket();
             SpotRouteBridge bridge = roomNode.createRouteBridge()) {
            String channel = "api";
            String endpoint = uniqueTcp();
            apiRouter.bind(endpoint);
            roomDealer.connect(endpoint);
            bridge.attachDealerChannel(channel, roomDealer);

            // 게임룸 노드의 route bridge가 API 채널로 outgame 요청을 보낸다.
            var replyFuture = bridge.request(channel, room.getRoutingId())
                .message(Message.from("get-profile"))
                .timeout(Duration.ofSeconds(5))
                .submit();

            // API 서버(ROUTER)는 요청을 폴링으로 받아 응답한다.
            try (Received received = new Received()) {
                long deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos();
                boolean served = false;
                while (!served && System.nanoTime() < deadline) {
                    try {
                        if (apiRouter.recv(received, RecvFlags.DONT_WAIT)) {
                            try (Message reply = Message.from("profile:level-7")) {
                                received.reply().message(reply).submit();
                            }
                            served = true;
                        }
                    } catch (RuntimeException noData) {
                        // no data yet
                    }
                    if (!served) {
                        Thread.sleep(10);
                    }
                }
            }

            List<Message> reply = replyFuture.toCompletableFuture().join();
            System.out.println(
                "[spot/channel] request \"get-profile\" -> reply \"" + reply.get(0).toUtf8String() + "\"");
            reply.forEach(Message::close);
        }
// --8<-- [end:doc]
    }

    private static String uniqueTcp() throws Exception {
        try (ServerSocket socket = new ServerSocket(0, 0, InetAddress.getByName("127.0.0.1"))) {
            return "tcp://127.0.0.1:" + socket.getLocalPort();
        }
    }
}
