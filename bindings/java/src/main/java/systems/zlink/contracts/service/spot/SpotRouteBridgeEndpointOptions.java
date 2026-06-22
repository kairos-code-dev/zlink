/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Options applied when a channel socket is attached to a SPOT route bridge. */
public final class SpotRouteBridgeEndpointOptions {
    private SpotRouteBridgeEndpointCapabilities capabilities =
        SpotRouteBridgeEndpointCapabilities.ROUTE_ONLY;
    private int inboundRelayPolicy;

    public SpotRouteBridgeEndpointCapabilities capabilities() {
        return capabilities;
    }

    public SpotRouteBridgeEndpointOptions capabilities(
        SpotRouteBridgeEndpointCapabilities value) {
        if (value == null) {
            throw new IllegalArgumentException("capabilities must not be null");
        }
        capabilities = value;
        return this;
    }

    public int inboundRelayPolicy() {
        return inboundRelayPolicy;
    }

    public SpotRouteBridgeEndpointOptions inboundRelayPolicy(int value) {
        inboundRelayPolicy = value;
        return this;
    }
}
