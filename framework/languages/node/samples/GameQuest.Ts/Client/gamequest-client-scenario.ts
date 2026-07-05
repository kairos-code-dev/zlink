import {
  PacketNames,
  QuestIds,
  QuestStatuses,
  collectItemReq,
  completeMissionReq,
  enterAreaReq,
  getQuestProgressReq,
  joinSessionReq,
  killMonsterReq,
  syncQuestProgressReq,
  unlockFeatureReq
} from '../Shared/Contracts/messages';
import type { ZLinkHttpClient } from '@zlink-systems/http-client';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type {
  CollectItemRes,
  CompleteMissionRes,
  EnterAreaRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  JoinSessionRes,
  KillMonsterRes,
  QuestCompletedNotify,
  QuestProgress,
  QuestProgressNotify,
  SyncQuestProgressRes,
  UnlockFeatureRes
} from '../Shared/Contracts/messages';

class GameQuestClientScenario {
  async run(
    apiA: ZLinkHttpClient,
    apiB: ZLinkHttpClient,
    missionA: ZLinkHttpClient,
    missionB: ZLinkHttpClient,
    apiAStream: ZlinkStreamConnector,
    apiBStream: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    await apiAStream.connect(signal);
    const joined = await apiAStream.request(joinSessionReq('player-alice'), Object)
      .packetName(PacketNames.joinSessionReq)
      .submit<JoinSessionRes>(signal);
    ensure(() => joined.activeQuests.length === 0);

    const firstProgress = apiAStream.waitFor<QuestProgressNotify>(PacketNames.questProgressNotify)
      .where((message) => message.payload.playerId === 'player-alice')
      .submit(signal);
    const firstKill = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-1'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => firstKill.eventId === 'player-alice-kill-1');
    const firstProgressPush = await firstProgress;
    ensure(() => firstProgressPush.payload.progress.questId === QuestIds.FirstHunt);
    ensure(() => firstProgressPush.payload.progress.currentCount === 1);

    const completeFirstHunt = apiAStream.waitFor<QuestCompletedNotify>(PacketNames.questCompletedNotify)
      .where((message) => message.payload.progress.questId === QuestIds.FirstHunt)
      .submit(signal);
    await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-2'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    const thirdKill = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => thirdKill.eventId === 'player-alice-kill-3');
    const firstHuntPush = await completeFirstHunt;
    ensure(() => firstHuntPush.payload.rewardGranted);
    ensure(() => firstHuntPush.payload.progress.status === QuestStatuses.RewardGranted);

    const duplicate = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => duplicate.eventId === thirdKill.eventId);

    const auctionComplete = apiAStream.waitFor<QuestCompletedNotify>(PacketNames.questCompletedNotify)
      .where((message) => message.payload.progress.questId === QuestIds.OpenAuction)
      .submit(signal);
    const auction = await apiAStream.request(unlockFeatureReq('player-alice', 'auction', 'unlock-auction'), Object)
      .packetName(PacketNames.unlockFeatureReq)
      .submit<UnlockFeatureRes>(signal);
    ensure(() => auction.eventId === 'player-alice-unlock-auction');
    const auctionCompletePush = await auctionComplete;
    ensure(() => auctionCompletePush.payload.rewardGranted);
    const snapshot = await apiA.post('/internal/snapshot')
      .body({ playerId: 'player-alice' })
      .fetch<GetGameplaySnapshotRes>();
    ensure(() => snapshot.unlockedFeatureIds.includes('auction'));

    const closeOwnerA = await missionA.post('/self-check/owner/player-alice/close').submitRaw();
    const closeOwnerB = await missionB.post('/self-check/owner/player-alice/close').submitRaw();
    ensure(() => closeOwnerA.status >= 200 && closeOwnerA.status < 300);
    ensure(() => closeOwnerB.status >= 200 && closeOwnerB.status < 300);

    const tutorial = await apiAStream.request(completeMissionReq('player-alice', 'tutorial', 'mission-tutorial'), Object)
      .packetName(PacketNames.completeMissionReq)
      .submit<CompleteMissionRes>(signal);
    ensure(() => tutorial.eventId === 'player-alice-mission-tutorial');
    const ruins = await apiAStream.request(enterAreaReq('player-alice', 'ruins', 'enter-ruins'), Object)
      .packetName(PacketNames.enterAreaReq)
      .submit<EnterAreaRes>(signal);
    ensure(() => ruins.eventId === 'player-alice-enter-ruins');

    const offlineItem = await apiAStream.request(collectItemReq('player-bob', 'healing-herb', 1, 'herb-1'), Object)
      .packetName(PacketNames.collectItemReq)
      .submit<CollectItemRes>(signal);
    ensure(() => offlineItem.eventId === 'player-bob-herb-1');

    await apiBStream.connect(signal);
    const bobJoined = await apiBStream.request(joinSessionReq('player-bob'), Object)
      .packetName(PacketNames.joinSessionReq)
      .submit<JoinSessionRes>(signal);
    const bobProgress = bobJoined.activeQuests.some((p) => p.questId === QuestIds.HerbGathering && p.currentCount === 1)
      ? bobJoined.activeQuests
      : await waitForStreamProjection(apiBStream, 'player-bob', (p) => p.questId === QuestIds.HerbGathering && p.currentCount === 1, signal);
    ensure(() => bobProgress.some((p) => p.questId === QuestIds.HerbGathering && p.currentCount === 1));

    const herbCompleted = apiBStream.waitFor<QuestCompletedNotify>(PacketNames.questCompletedNotify)
      .where((message) => message.payload.progress.questId === QuestIds.HerbGathering)
      .submit(signal);
    const onlineItem = await apiBStream.request(collectItemReq('player-bob', 'healing-herb', 4, 'herb-2'), Object)
      .packetName(PacketNames.collectItemReq)
      .submit<CollectItemRes>(signal);
    ensure(() => onlineItem.eventId === 'player-bob-herb-2');
    const herbPush = await herbCompleted;
    ensure(() => herbPush.payload.playerId === 'player-bob');
    ensure(() => herbPush.payload.rewardGranted);
    ensure(() => herbPush.payload.progress.status === QuestStatuses.RewardGranted);

    const deleted = await apiA.post(`/self-check/projection/player-bob/${QuestIds.HerbGathering}/delete`).submitRaw();
    ensure(() => deleted.status >= 200 && deleted.status < 300);
    const missingProjection = await getStreamProjection(apiBStream, 'player-bob', signal);
    ensure(() => missingProjection.every((progress) => progress.questId !== QuestIds.HerbGathering));
    const rebuilt = await apiA.post(`/self-check/projection/player-bob/${QuestIds.HerbGathering}/rebuild`).fetch<QuestProgress>();
    ensure(() => rebuilt.questId === QuestIds.HerbGathering);
    ensure(() => rebuilt.status === QuestStatuses.RewardGranted);
    const rebuiltProjection = await getStreamProjection(apiBStream, 'player-bob', signal);
    ensure(() => rebuiltProjection.some((progress) =>
      progress.questId === QuestIds.HerbGathering && progress.status === QuestStatuses.RewardGranted));

    const missed = await apiB.post('/self-check/gameplay/kill-without-publish/player-alice').submitRaw();
    ensure(() => missed.status >= 200 && missed.status < 300);
    const sync = await apiAStream.request(syncQuestProgressReq('player-alice'), Object)
      .packetName(PacketNames.syncQuestProgressReq)
      .submit<SyncQuestProgressRes>(signal);
    ensure(() => sync.updatedQuests.some((progress) =>
      progress.questId === QuestIds.FirstHunt && progress.currentCount >= 4));
    const reconciled = await waitForProjection(apiB, 'player-alice', (progress) =>
      progress.questId === QuestIds.FirstHunt && progress.currentCount >= 4, signal);
    ensure(() => reconciled.some((progress) => progress.questId === QuestIds.FirstHunt && progress.currentCount >= 4));

    await apiAStream.close();
    const assertion = await waitForServerAssertion(apiA, signal);
    ensure(() => assertion.passed);
    console.log('gamequest-server-evidence=completed');
  }
}

async function waitForServerAssertion(api: ZLinkHttpClient, signal?: AbortSignal): Promise<GameQuestServerAssertRes> {
  let last: GameQuestServerAssertRes | undefined;
  for (let i = 0; i < 80; i++) {
    last = await api.post('/self-check/assert').fetch<GameQuestServerAssertRes>();
    if (last.passed) {
      return last;
    }
    await delay(50, signal);
  }
  return last ?? { passed: false, evidence: [] };
}

async function waitForProjection(
  api: ZLinkHttpClient,
  playerId: string,
  predicate: (progress: QuestProgress) => boolean,
  signal?: AbortSignal
): Promise<QuestProgress[]> {
  for (let i = 0; i < 80; i++) {
    const response = await api.get(`/quest/progress/${playerId}`).fetch<GetQuestProgressRes>();
    if (response.activeQuests.some(predicate)) {
      return response.activeQuests;
    }
    await delay(50, signal);
  }
  return (await api.get(`/quest/progress/${playerId}`).fetch<GetQuestProgressRes>()).activeQuests;
}

async function waitForStreamProjection(
  connector: ZlinkStreamConnector,
  playerId: string,
  predicate: (progress: QuestProgress) => boolean,
  signal?: AbortSignal
): Promise<QuestProgress[]> {
  for (let i = 0; i < 80; i++) {
    const projection = await getStreamProjection(connector, playerId, signal);
    if (projection.some(predicate)) {
      return projection;
    }
    await delay(50, signal);
  }
  return await getStreamProjection(connector, playerId, signal);
}

async function getStreamProjection(
  connector: ZlinkStreamConnector,
  playerId: string,
  signal?: AbortSignal
): Promise<QuestProgress[]> {
  const response = await connector.request(getQuestProgressReq(playerId), Object)
    .packetName(PacketNames.getQuestProgressReq)
    .submit<GetQuestProgressRes>(signal);
  return response.activeQuests;
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${condition.toString()}`);
  }
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

export { GameQuestClientScenario };
