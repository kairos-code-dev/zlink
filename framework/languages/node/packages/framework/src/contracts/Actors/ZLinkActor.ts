import type { ZLinkActorContext } from './ZLinkActorContext';
import type { ZLinkActorJoinCompletion } from './ZLinkActorFactory';

export interface ZLinkActor {
  /** Must project `context.actorId`; Framework rejects a mismatched factory result. */
  readonly actorId: string;
  readonly context: ZLinkActorContext;
  configure?(): void;
  onJoinCompleted?(completion: ZLinkActorJoinCompletion): Promise<void>;
}
