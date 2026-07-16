import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';
import { ZoneIds } from '../../../../../Shared/spec';
import type { ZoneId } from '../../../../../Shared/spec';

class PlayerActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;

  constructor(
    readonly actorId: string,
    public x = 25,
    public y = 25,
    public zoneId: ZoneId = ZoneIds.northWest,
    public isBot = false,
    public dirX = 0,
    public dirY = 0
  ) {}

  push(payload: unknown): void {
    if (this.isBot) return;
    this.context.boundSession.send(payload).submit();
  }
}

export { PlayerActor };
