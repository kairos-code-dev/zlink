/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Capabilities enabled for one borrowed SPOT route bridge endpoint. */
public enum SpotRouteBridgeEndpointCapabilities {
    NONE(0),
    SPOT_ROUTE(0x00000001),
    CHANNEL_INBOUND(0x00000002),
    ROUTE_ONLY(SPOT_ROUTE.value),
    ROUTE_WITH_CHANNEL_INBOUND(SPOT_ROUTE.value | CHANNEL_INBOUND.value);

    private final int value;

    SpotRouteBridgeEndpointCapabilities(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }
}
