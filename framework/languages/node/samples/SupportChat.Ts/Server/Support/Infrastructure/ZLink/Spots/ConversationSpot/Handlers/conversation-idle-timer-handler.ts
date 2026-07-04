import { ConversationStatuses } from '../../../../../../../Shared/Contracts/messages';

class ConversationIdleTimerHandler {
  waitingStatus(): string {
    return ConversationStatuses.WaitingForClose;
  }
}

export { ConversationIdleTimerHandler };
