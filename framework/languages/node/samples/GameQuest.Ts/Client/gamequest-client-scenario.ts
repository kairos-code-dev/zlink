import {
  collectItemReq,
  completeMissionReq,
  deleteQuestProjectionReq,
  enterAreaReq,
  gameQuestServerAssertReq,
  getGameplaySnapshotReq,
  getQuestProgressReq,
  killMonsterReq,
  rebuildQuestProjectionReq,
  subscribeQuestReq,
  syncQuestProgressReq,
  unlockFeatureReq
} from '../Shared/Contracts/messages';
import { SampleNames } from '../Shared/Configuration/sample-names';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  EventRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  QuestProgress,
  SubscribeQuestRes,
  SyncQuestProgressRes
} from '../Shared/Contracts/messages';

class GameQuestClientScenario {
  async run(channels: ZLinkChannelClient): Promise<void> {
    const subscribed = await request<SubscribeQuestRes>(channels, subscribeQuestReq('player-alice'));
    ensure(subscribed.activeQuests.length === 0);

    const firstKill = await request<EventRes>(channels, killMonsterReq('player-alice', 'wolf', 'forest', 'kill-1'));
    ensure(firstKill.eventId === 'player-alice-kill-1');
    const secondKill = await request<EventRes>(channels, killMonsterReq('player-alice', 'wolf', 'forest', 'kill-2'));
    ensure(secondKill.eventId === 'player-alice-kill-2');
    const thirdKill = await request<EventRes>(channels, killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'));
    ensure(thirdKill.eventId === 'player-alice-kill-3');
    const duplicate = await request<EventRes>(channels, killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'));
    ensure(duplicate.eventId === thirdKill.eventId);

    const auction = await request<EventRes>(channels, unlockFeatureReq('player-alice', 'auction', 'unlock-auction'));
    ensure(auction.eventId === 'player-alice-unlock-auction');
    const snapshot = await request<GetGameplaySnapshotRes>(channels, getGameplaySnapshotReq('player-alice'));
    ensure(snapshot.unlockedFeatureIds.includes('auction'));

    const tutorial = await request<EventRes>(channels, completeMissionReq('player-alice', 'tutorial', 'mission-tutorial'));
    ensure(tutorial.eventId === 'player-alice-mission-tutorial');
    const ruins = await request<EventRes>(channels, enterAreaReq('player-alice', 'ruins', 'enter-ruins'));
    ensure(ruins.eventId === 'player-alice-enter-ruins');

    const offlineItem = await request<EventRes>(channels, collectItemReq('player-bob', 'healing-herb', 1, 'herb-1'));
    ensure(offlineItem.eventId === 'player-bob-herb-1');
    const bobSubscribed = await request<SubscribeQuestRes>(channels, subscribeQuestReq('player-bob'));
    ensure(bobSubscribed.activeQuests.some((progress) => progress.questId === 'herb-gathering' && progress.currentCount === 1));
    const onlineItem = await request<EventRes>(channels, collectItemReq('player-bob', 'healing-herb', 4, 'herb-2'));
    ensure(onlineItem.eventId === 'player-bob-herb-2');
    const bobProgress = await request<GetQuestProgressRes>(channels, getQuestProgressReq('player-bob'));
    ensure(bobProgress.activeQuests.some((progress) => progress.questId === 'herb-gathering' && progress.status === 'RewardGranted'));

    await request<QuestProgress | undefined>(channels, deleteQuestProjectionReq('player-bob', 'herb-gathering'));
    const missingProjection = await request<GetQuestProgressRes>(channels, getQuestProgressReq('player-bob'));
    ensure(missingProjection.activeQuests.every((progress) => progress.questId !== 'herb-gathering'));
    const rebuilt = await request<QuestProgress>(channels, rebuildQuestProjectionReq('player-bob', 'herb-gathering'));
    ensure(rebuilt.questId === 'herb-gathering' && rebuilt.status === 'RewardGranted');

    const sync = await request<SyncQuestProgressRes>(channels, syncQuestProgressReq('player-alice'));
    ensure(sync.updatedQuests.some((progress) => progress.questId === 'first-hunt' && progress.currentCount >= 4));

    const assertion = await request<GameQuestServerAssertRes>(channels, gameQuestServerAssertReq());
    ensure(assertion.passed);
    console.log('gamequest-server-evidence=completed');
  }
}

async function request<TResponse>(channels: ZLinkChannelClient, payload: unknown): Promise<TResponse> {
  return await channels
    .requestToChannel(SampleNames.questChannel, payload)
    .submit<TResponse>();
}

function ensure(condition: boolean): void {
  if (!condition) {
    throw new Error('GameQuest scenario assertion failed.');
  }
}

export {
  GameQuestClientScenario
};
