package systems.zlink.samples.gamequest.server.questmission.infrastructure.zlink;

import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.questmission.application.QuestEventProcessor;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@Component
public final class ZLinkGameApiSnapshotClient implements QuestEventProcessor.GameApiSnapshotClient {
    private final ZLinkClient channels;

    public ZLinkGameApiSnapshotClient(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public Messages.GetGameplaySnapshotRes read(String apiName, String playerId) {
        return channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(apiName),
                new Messages.GetGameplaySnapshotReq(playerId))
            .timeout(SampleNames.RequestTimeout)
            .await(Messages.GetGameplaySnapshotRes.class);
    }
}
