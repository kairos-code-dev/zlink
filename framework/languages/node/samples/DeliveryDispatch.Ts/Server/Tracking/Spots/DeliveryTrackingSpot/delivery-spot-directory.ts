import type { DeliveryStatusChanged } from '../../../../Shared/Contracts/messages';

type DeliveryRecorder = {
  record(status: DeliveryStatusChanged): void;
};

class DeliverySpotDirectory {
  private readonly spots = new Map<string, DeliveryRecorder>();

  add(deliveryId: string, spot: DeliveryRecorder): void {
    this.spots.set(deliveryId, spot);
  }

  require(deliveryId: string): DeliveryRecorder {
    const spot = this.spots.get(deliveryId);
    if (spot === undefined) {
      throw new Error(`Delivery spot '${deliveryId}' was not created.`);
    }
    return spot;
  }
}

export { DeliverySpotDirectory };
