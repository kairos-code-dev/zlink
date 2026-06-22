package systems.zlink.samples.gamequest.server.gameapi.infrastructure.zlink;

import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@Component
public final class ZLinkQuestProgressSynchronizer implements GameplayActionService.QuestProgressSynchronizer {
    private final ZLinkClient channels;

    public ZLinkQuestProgressSynchronizer(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public Messages.SyncQuestProgressRes sync(String missionName, String playerId) {
        return channels
            .requestToChannel(
                SampleNames.questMissionChannel(missionName),
                new Messages.SyncQuestProgressReq(playerId))
            .timeout(SampleNames.RequestTimeout)
            .await(Messages.SyncQuestProgressRes.class);
    }
}
