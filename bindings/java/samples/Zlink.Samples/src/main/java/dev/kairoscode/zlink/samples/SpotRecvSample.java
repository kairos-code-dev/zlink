/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.spot.Spot;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        try (Context ctx = new Context();
             Spot spot = new Spot(ctx)) {
            SampleSupport.subscribeAndAwaitSpotRecvReady(spot, "sample.topic");
            spot.publish("sample.topic", Message.copyOfUtf8("spot-sample"));
            try (var received = spot.recv()) {
                System.out.println("spot recv: " + received.topicId()
                  + " -> " + received.firstPart().toUtf8String());
            }
        }
    }
}
