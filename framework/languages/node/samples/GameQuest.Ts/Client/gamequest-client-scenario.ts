import {
  collectItemReq,
  completeMissionReq,
  enterAreaReq,
  getGameplaySnapshotReq,
  getQuestProgressReq,
  killMonsterReq,
  subscribeQuestReq,
  syncQuestProgressReq,
  unlockFeatureReq
} from '../Shared/Contracts/messages';
import type { ZLinkHttpClient } from '@zlink-systems/http-client';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type {
  EventRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  QuestProgress,
  QuestCompletedNotify,
  QuestProgressNotify,
  SubscribeQuestRes,
  SyncQuestProgressRes
} from '../Shared/Contracts/messages';

class GameQuestClientScenario {
  async run(
    apiA: ZLinkHttpClient,
    apiB: ZLinkHttpClient,
    apiAStream: ZlinkStreamConnector,
    apiBStream: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    await apiAStream.connect(signal);
    const subscribed = await apiAStream.request(subscribeQuestReq('player-alice'), Object)
      .packetName('SubscribeQuestReq')
      .submit<SubscribeQuestRes>(signal);
    ensure(Array.isArray(subscribed.activeQuests));

    const firstProgress = apiAStream.waitFor<QuestProgressNotify>('QuestProgressNotify')
      .where((message) => message.payload.playerId === 'player-alice' && message.payload.progress.questId === 'first-hunt')
      .submit(signal);
    const firstKill = await post<EventRes>(apiA, '/combat/kill', killMonsterReq('player-alice', 'wolf', 'forest', 'kill-1'));
    ensure(firstKill.eventId === 'player-alice-kill-1');
    ensure((await firstProgress).payload.progress.currentCount === 1);
    const firstCompleted = apiAStream.waitFor<QuestCompletedNotify>('QuestCompletedNotify')
      .where((message) => message.payload.playerId === 'player-alice' && message.payload.progress.questId === 'first-hunt')
      .submit(signal);
    const secondKill = await post<EventRes>(apiA, '/combat/kill', killMonsterReq('player-alice', 'wolf', 'forest', 'kill-2'));
    ensure(secondKill.eventId === 'player-alice-kill-2');
    const thirdKill = await post<EventRes>(apiA, '/combat/kill', killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'));
    ensure(thirdKill.eventId === 'player-alice-kill-3');
    ensure((await firstCompleted).payload.rewardGranted);
    const duplicate = await post<EventRes>(apiA, '/combat/kill', killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'));
    ensure(duplicate.eventId === thirdKill.eventId);

    const auctionCompleted = apiAStream.waitFor<QuestCompletedNotify>('QuestCompletedNotify')
      .where((message) => message.payload.playerId === 'player-alice' && message.payload.progress.questId === 'open-auction')
      .submit(signal);
    const auction = await post<EventRes>(apiA, '/feature/unlock', unlockFeatureReq('player-alice', 'auction', 'unlock-auction'));
    ensure(auction.eventId === 'player-alice-unlock-auction');
    ensure((await auctionCompleted).payload.rewardGranted);
    const snapshot = await post<GetGameplaySnapshotRes>(apiA, '/internal/snapshot', getGameplaySnapshotReq('player-alice'));
    ensure(snapshot.unlockedFeatureIds.includes('auction'));

    const tutorial = await post<EventRes>(apiA, '/mission/complete', completeMissionReq('player-alice', 'tutorial', 'mission-tutorial'));
    ensure(tutorial.eventId === 'player-alice-mission-tutorial');
    const ruins = await post<EventRes>(apiA, '/world/enter', enterAreaReq('player-alice', 'ruins', 'enter-ruins'));
    ensure(ruins.eventId === 'player-alice-enter-ruins');

    const offlineItem = await post<EventRes>(apiA, '/inventory/collect', collectItemReq('player-bob', 'healing-herb', 1, 'herb-1'));
    ensure(offlineItem.eventId === 'player-bob-herb-1');
    await apiBStream.connect(signal);
    const bobSubscribed = await apiBStream.request(subscribeQuestReq('player-bob'), Object)
      .packetName('SubscribeQuestReq')
      .submit<SubscribeQuestRes>(signal);
    ensure(bobSubscribed.activeQuests.some((progress) => progress.questId === 'herb-gathering' && progress.currentCount === 1));
    const herbCompleted = apiBStream.waitFor<QuestCompletedNotify>('QuestCompletedNotify')
      .where((message) => message.payload.playerId === 'player-bob' && message.payload.progress.questId === 'herb-gathering')
      .submit(signal);
    const onlineItem = await post<EventRes>(apiB, '/inventory/collect', collectItemReq('player-bob', 'healing-herb', 4, 'herb-2'));
    ensure(onlineItem.eventId === 'player-bob-herb-2');
    ensure((await herbCompleted).payload.rewardGranted);
    const bobProgress = await apiBStream.request(getQuestProgressReq('player-bob'), Object)
      .packetName('GetQuestProgressReq')
      .submit<GetQuestProgressRes>(signal);
    ensure(bobProgress.activeQuests.some((progress) => progress.questId === 'herb-gathering' && progress.status === 'RewardGranted'));

    await apiA.post('/self-check/projection/player-bob/herb-gathering/delete').fetch<QuestProgress | undefined>();
    const missingProjection = await apiBStream.request(getQuestProgressReq('player-bob'), Object)
      .packetName('GetQuestProgressReq')
      .submit<GetQuestProgressRes>(signal);
    ensure(missingProjection.activeQuests.every((progress) => progress.questId !== 'herb-gathering'));
    const rebuilt = await apiA.post('/self-check/projection/player-bob/herb-gathering/rebuild').fetch<QuestProgress>();
    ensure(rebuilt.questId === 'herb-gathering' && rebuilt.status === 'RewardGranted');

    await apiB.post('/self-check/gameplay/kill-without-publish/player-alice').fetch<Record<string, unknown>>();
    const sync = await apiAStream.request(syncQuestProgressReq('player-alice'), Object)
      .packetName('SyncQuestProgressReq')
      .submit<SyncQuestProgressRes>(signal);
    ensure(sync.updatedQuests.some((progress) => progress.questId === 'first-hunt' && progress.currentCount >= 4));
    const reconciled = await apiB.get('/quest/progress/player-alice').fetch<GetQuestProgressRes>();
    ensure(reconciled.activeQuests.some((progress) => progress.questId === 'first-hunt' && progress.currentCount >= 4));

    const assertion = await waitForServerAssertion(apiA, signal);
    ensure(assertion.passed);
    console.log('gamequest-server-evidence=completed');
  }
}

async function post<TResponse>(api: ZLinkHttpClient, path: string, payload: unknown): Promise<TResponse> {
  return await retry(() => api.post(path).body(stripPacket(payload)).fetch<TResponse>());
}

async function waitForServerAssertion(api: ZLinkHttpClient, signal?: AbortSignal): Promise<GameQuestServerAssertRes> {
  let last: GameQuestServerAssertRes | undefined;
  for (let attempt = 0; attempt < 80 && signal?.aborted !== true; attempt += 1) {
    last = await retry(() => api.post('/self-check/assert').fetch<GameQuestServerAssertRes>());
    if (last.passed) {
      return last;
    }
    await delay(250, signal);
  }
  return last ?? { passed: false, evidence: [] };
}

async function retry<T>(operation: () => Promise<T>): Promise<T> {
  let lastError: unknown;
  for (let attempt = 0; attempt < 40; attempt += 1) {
    try {
      return await operation();
    } catch (error) {
      lastError = error;
      await delay(250);
    }
  }
  throw lastError instanceof Error ? lastError : new Error(String(lastError));
}

function delay(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(resolve, ms);
    signal?.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('Operation aborted.', 'AbortError'));
    }, { once: true });
  });
}

function stripPacket(payload: unknown): unknown {
  if (typeof payload !== 'object' || payload === null) {
    return payload;
  }
  const { packetName, ...body } = payload as Record<string, unknown>;
  void packetName;
  return body;
}

function ensure(condition: boolean): void {
  if (!condition) {
    throw new Error('GameQuest scenario assertion failed.');
  }
}

export {
  GameQuestClientScenario
};
