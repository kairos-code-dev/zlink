import { questMissionRouteRid } from '../../../Shared/Configuration/sample-names';

class QuestOwnerRouter {
  constructor(private readonly instanceId: string) {}

  routeRid(playerId: string): string {
    return questMissionRouteRid(playerId);
  }

  isLocalOwner(playerId: string): boolean {
    return this.routeRid(playerId) === this.instanceId;
  }
}

export { QuestOwnerRouter };
