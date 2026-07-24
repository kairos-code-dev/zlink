package systems.zlink.framework.spots;

import java.util.Map;
import systems.zlink.framework.channels.ZLinkSendCall;

public interface ZLinkSpotSendCall extends ZLinkSendCall {
    default ZLinkSpotSendCall instanceSpot() {
        throw new UnsupportedOperationException(
            "Instance Spot activation is not available");
    }
    default ZLinkSpotSendCall instanceSpot(String stableType) {
        throw new UnsupportedOperationException(
            "Instance Spot activation is not available");
    }
    default ZLinkSpotSendCall inMesh(String meshName) {
        throw new UnsupportedOperationException(
            "Spot mesh selection is not available");
    }
    @Override
    default ZLinkSpotSendCall metadata(String key, String value) {
        throw new UnsupportedOperationException("Spot metadata is not available");
    }
    @Override
    default ZLinkSpotSendCall metadata(Map<String, String> metadata) {
        throw new UnsupportedOperationException("Spot metadata is not available");
    }
}
