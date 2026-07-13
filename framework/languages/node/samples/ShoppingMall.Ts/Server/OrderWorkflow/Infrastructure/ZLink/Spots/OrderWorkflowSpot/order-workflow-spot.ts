import type { ZLinkSpot, ZLinkSpotContext } from '@zlink-systems/framework';

class OrderWorkflowSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
}

export { OrderWorkflowSpot };
