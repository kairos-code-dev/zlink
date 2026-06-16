package systems.zlink.samples.kotlin.shoppingmallcheckout.server.commerceapi

import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleNames

/** Parsed `--instance` selector for a CommerceApi process. */
data class CommerceApiInstanceOptions(val instanceId: String) {
    companion object {
        fun fromArgs(args: Array<String>): CommerceApiInstanceOptions {
            var instanceId = SampleNames.ApiInstanceA
            for (i in 0 until args.size - 1) {
                if (args[i] == "--instance") {
                    instanceId = args[i + 1]
                }
            }
            return CommerceApiInstanceOptions(instanceId)
        }

        fun peerInstance(instanceId: String): String =
            if (instanceId == SampleNames.ApiInstanceB) SampleNames.ApiInstanceA else SampleNames.ApiInstanceB
    }
}
