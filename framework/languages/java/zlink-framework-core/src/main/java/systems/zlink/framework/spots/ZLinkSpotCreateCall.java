package systems.zlink.framework.spots;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSpotCreateCall {
    ZLinkSpotCreateCall inMesh(String meshName);
    ZLinkSpotCreateCall request(Object request);
    ZLinkSpotCreateCall request(ZLinkMessage request);
    ZLinkSpotCreateCall placementProfile(String placementProfile);
    ZLinkSpotCreateCall affinityKey(String affinityKey);
    ZLinkSpotCreateCall timeout(Duration timeout);
    CompletionStage<ZLinkSpotCreateResult> submit();
}
