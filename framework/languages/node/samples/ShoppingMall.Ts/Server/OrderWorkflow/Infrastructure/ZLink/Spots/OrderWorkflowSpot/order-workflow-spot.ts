import type { ZLinkSpot, ZLinkSpotContext } from '@zlink-systems/framework';

class OrderWorkflowSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
}

export { OrderWorkflowSpot };
