import { PacketNames } from '../../Shared/Contracts/messages';
import type { ZLinkSessionContext } from '@zlink-systems/framework';
import type { QuestCompletedNotify, QuestProgress, QuestProgressNotify } from '../../Shared/Contracts/messages';

class PlayerSessionDirectory {
  private readonly sessions = new Map<string, ZLinkSessionContext>();
  private readonly players = new Map<string, Set<string>>();

  add(context: ZLinkSessionContext): void {
    this.sessions.set(context.sessionId, context);
  }

  remove(context: ZLinkSessionContext): void {
    this.sessions.delete(context.sessionId);
    for (const sessions of this.players.values()) {
      sessions.delete(context.sessionId);
    }
  }

  subscribe(context: ZLinkSessionContext, playerId: string): void {
    this.add(context);
    let sessions = this.players.get(playerId);
    if (sessions === undefined) {
      sessions = new Set();
      this.players.set(playerId, sessions);
    }
    sessions.add(context.sessionId);
  }

  async publish(playerId: string, progress: QuestProgress[]): Promise<void> {
    const sessions = this.players.get(playerId);
    if (sessions === undefined) {
      return;
    }
    for (const sessionId of sessions) {
      const session = this.sessions.get(sessionId);
      if (session === undefined) {
        continue;
      }
      for (const item of progress) {
        await session.client.send({ playerId, progress: item } satisfies QuestProgressNotify)
          .packetName(PacketNames.questProgressNotify)
          .submit();
        if (item.status === 'RewardGranted') {
          await session.client.send({ playerId, progress: item, rewardGranted: true } satisfies QuestCompletedNotify)
            .packetName(PacketNames.questCompletedNotify)
            .submit();
        }
      }
    }
  }
}

export {
  PlayerSessionDirectory
};
