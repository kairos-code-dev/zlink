import { questMissionRouteRid } from '../../Configuration/gamequest-routing';

class QuestOwnerRouter {
  routeRid(playerId: string): string {
    return questMissionRouteRid(playerId);
  }

  spotRid(playerId: string): string {
    return `player:${playerId}`;
  }
}

export {
  QuestOwnerRouter
};
