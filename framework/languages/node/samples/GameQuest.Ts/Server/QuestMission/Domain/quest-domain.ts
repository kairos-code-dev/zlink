import type { QuestStatus } from '../../../Shared/Contracts/messages';

const QuestIds = {
  firstHunt: 'first-hunt',
  herbGathering: 'herb-gathering',
  openAuction: 'open-auction',
  tutorial: 'tutorial',
  ruinsExplorer: 'ruins-explorer'
} as const;

function questIdForArea(areaId: string): string {
  return areaId === 'ruins' ? QuestIds.ruinsExplorer : areaId;
}

function questStatus(currentCount: number, requiredCount: number): QuestStatus {
  return currentCount >= requiredCount ? 'RewardGranted' : 'InProgress';
}

export {
  QuestIds,
  questIdForArea,
  questStatus
};
