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
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.List;

// --8<-- [start:doc]
public final class SpotChannelExample {
    public static void main(String[] args) throws Exception {
        try (Context ctx = Zlink.createContext();
             SpotNode roomNode = ctx.createSpotNode();
             Spot room = roomNode.createSpot();
             DealerSocket roomDealer = ctx.createDealerSocket();
             RouterSocket apiRouter = ctx.createRouterSocket()) {
            String channel = "api";
            String endpoint = uniqueTcp();
            apiRouter.bind(endpoint);
            roomDealer.connect(endpoint);
            // "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
            roomNode.attachChannelDealerManual(channel, roomDealer);

            // 게임룸이 API 채널로 outgame 요청을 보낸다.
            var replyFuture = room.requestToChannel(channel)
                .message(Message.from("get-profile"))
                .timeout(Duration.ofSeconds(5))
                .submitAsync();

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

            List<Message> reply = replyFuture.join();
            System.out.println(
                "[spot/channel] request \"get-profile\" -> reply \"" + reply.get(0).toUtf8String() + "\"");
            reply.forEach(Message::close);
        }
    }

    private static String uniqueTcp() throws Exception {
        try (ServerSocket socket = new ServerSocket(0, 0, InetAddress.getByName("127.0.0.1"))) {
            return "tcp://127.0.0.1:" + socket.getLocalPort();
        }
    }
}
// --8<-- [end:doc]
