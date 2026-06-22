package systems.zlink.samples.gamequest.server.questmission.infrastructure.zlink;

import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.questmission.application.QuestEventProcessor;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@Component
public final class ZLinkQuestProgressNotifier implements QuestEventProcessor.QuestProgressNotifier {
    private final ZLinkClient channels;

    public ZLinkQuestProgressNotifier(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public boolean notify(String sourceApi, Messages.NotifyQuestProgressReq request) {
        String apiName = "api-b".equals(sourceApi) ? "api-b" : "api-a";
        try {
            Messages.NotifyQuestProgressRes delivered = channels
                .requestToChannel(SampleNames.gameApiActionChannel(apiName), request)
                .timeout(SampleNames.RequestTimeout)
                .await(Messages.NotifyQuestProgressRes.class);
            return delivered != null && delivered.delivered();
        } catch (RuntimeException error) {
            System.err.printf(
                "gamequest mission projection kept while stream notify failed. player=%s%n",
                request.playerId());
            return false;
        }
    }
}
