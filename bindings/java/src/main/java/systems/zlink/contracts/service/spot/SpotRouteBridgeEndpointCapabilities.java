/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Capabilities enabled for one borrowed SPOT route bridge endpoint. */
public enum SpotRouteBridgeEndpointCapabilities {
    NONE(0),
    SPOT_ROUTE(0x00000001),
    ROUTE_ONLY(SPOT_ROUTE.value);

    private final int value;

    SpotRouteBridgeEndpointCapabilities(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }
}
