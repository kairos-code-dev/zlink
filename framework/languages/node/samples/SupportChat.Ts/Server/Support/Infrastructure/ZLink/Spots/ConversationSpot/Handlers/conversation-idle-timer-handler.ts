import { zlinkSpotTimerHandler } from '@zlink-systems/nestjs';
import { ZLinkTimerOverrunPolicy } from '@zlink-systems/framework';
import { SampleTimings } from '../../../../../../Configuration/sample-names';
import { ConversationSpot } from '../conversation-spot';
import type { ZLinkSpotTimerHandler, ZLinkTimerTick } from '@zlink-systems/framework';

// The idle timer only forwards the time signal to the Spot. The Conversation domain decides
// whether the conversation transitions to WaitingForClose (idle) or Closed (close grace).
@zlinkSpotTimerHandler({
  spot: () => ConversationSpot,
  name: 'conversation-idle',
  periodMs: SampleTimings.idleCheckPeriodMs,
  options: {
    overrunPolicy: ZLinkTimerOverrunPolicy.DelayNextTick,
    stopOnUnhandledException: true
  }
})
class ConversationIdleTimerHandler implements ZLinkSpotTimerHandler<ConversationSpot> {
  async handle(conversation: ConversationSpot, tick: ZLinkTimerTick): Promise<void> {
    void tick;
    await conversation.checkIdle();
  }
}

export { ConversationIdleTimerHandler };
