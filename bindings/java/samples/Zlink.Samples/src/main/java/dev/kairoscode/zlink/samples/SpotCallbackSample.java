/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.spot.Spot;
import java.util.concurrent.CountDownLatch;

public final class SpotCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        CountDownLatch delivered = new CountDownLatch(1);
        try (Context ctx = new Context();
             Spot spot = new Spot(ctx)) {
            spot.onSubscribe((routingId, topic, received) -> {
                try (received) {
                    System.out.println("spot callback: " + topic + " -> "
                      + received.firstPart().toUtf8String());
                    delivered.countDown();
                }
            });
            SampleSupport.subscribeAndAwaitSpotFilterApplied(spot,
              "sample.topic");
            spot.publish("sample.topic", Message.copyOfUtf8("spot-callback"));
            SampleSupport.await(delivered, "spot callback");
        }
    }
}
