package systems.zlink.framework.spots;

import java.time.Duration;
import java.util.Map;
import systems.zlink.framework.channels.ZLinkRequestCall;

public interface ZLinkSpotRequestCall extends ZLinkRequestCall {
    default ZLinkSpotRequestCall instanceSpot() {
        throw new UnsupportedOperationException(
            "Instance Spot activation is not available");
    }
    default ZLinkSpotRequestCall instanceSpot(String stableType) {
        throw new UnsupportedOperationException(
            "Instance Spot activation is not available");
    }
    default ZLinkSpotRequestCall inMesh(String meshName) {
        throw new UnsupportedOperationException(
            "Spot mesh selection is not available");
    }
    @Override
    default ZLinkSpotRequestCall metadata(String key, String value) {
        throw new UnsupportedOperationException("Spot metadata is not available");
    }
    @Override
    default ZLinkSpotRequestCall metadata(Map<String, String> metadata) {
        throw new UnsupportedOperationException("Spot metadata is not available");
    }
    @Override ZLinkSpotRequestCall timeout(Duration timeout);
}
