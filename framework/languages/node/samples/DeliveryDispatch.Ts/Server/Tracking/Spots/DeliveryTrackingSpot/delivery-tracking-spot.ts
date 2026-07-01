import { DeliverySpotDirectory } from './delivery-spot-directory';
import type {
  ZLinkMessage,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkSpotCreateResponse
} from '@zlink-systems/framework';
import type {
  DeliverySpotCreateReq,
  DeliverySpotJoinRes,
  DeliveryStatusReq
} from '../../../../Shared/Contracts/messages';
import type { CustomerActor } from '../../customer-actor';

class DeliveryTrackingSpot implements ZLinkSpot<CustomerActor> {
  private static directory: DeliverySpotDirectory | null = null;
  readonly context!: ZLinkSpotContext<CustomerActor>;
  private readonly customers = new Map<string, CustomerActor>();
  private readonly history: DeliveryStatusReq[] = [];
  private deliveryId = '';

  static useDirectory(directory: DeliverySpotDirectory): void {
    DeliveryTrackingSpot.directory = directory;
  }

  async onCreate(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse> {
    const create = request.decode<DeliverySpotCreateReq>(Object as never);
    this.deliveryId = create.deliveryId;
    this.requireDirectory().add(this.deliveryId, this);
    return { accepted: true, reply: { deliveryId: this.deliveryId } };
  }

  async onActorJoin(actor: CustomerActor, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    const join = request.decode<{ deliveryId: string; customerId: string }>(Object as never);
    if (join.deliveryId !== this.deliveryId) {
      return { accepted: false };
    }
    this.customers.set(actor.actorId, actor);
    console.error(`deliverydispatch tracking spot: joined delivery=${join.deliveryId} customer=${actor.actorId}`);
    return { accepted: true, reply: { deliveryId: join.deliveryId, customerId: actor.actorId } satisfies DeliverySpotJoinRes };
  }

  record(status: DeliveryStatusReq): void {
    this.history.push(status);
  }

  private requireDirectory(): DeliverySpotDirectory {
    if (DeliveryTrackingSpot.directory === null) {
      throw new Error('DeliverySpotDirectory is not configured.');
    }
    return DeliveryTrackingSpot.directory;
  }
}

export { DeliveryTrackingSpot };
