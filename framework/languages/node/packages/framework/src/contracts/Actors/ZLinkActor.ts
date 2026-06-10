import type { ZLinkActorContext } from './ZLinkActorContext';

export interface ZLinkActor {
  readonly actorId: string;
  readonly context: ZLinkActorContext;
  configure?(): void;
}
