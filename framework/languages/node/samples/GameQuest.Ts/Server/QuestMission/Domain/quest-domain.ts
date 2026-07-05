import { QuestIds } from '../../../Shared/Contracts/messages';

const QuestDomain = {
  questIdForArea(areaId: string): string {
    return areaId === 'ruins' ? QuestIds.RuinsExplorer : QuestIds.FirstHunt;
  }
};

export { QuestDomain };
