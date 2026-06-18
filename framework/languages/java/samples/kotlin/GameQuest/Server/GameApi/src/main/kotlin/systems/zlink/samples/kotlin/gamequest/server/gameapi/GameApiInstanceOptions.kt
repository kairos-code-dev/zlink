package systems.zlink.samples.kotlin.gamequest.server.gameapi

/** Parsed `--api` selector for a GameApi process (`api-a`/`api-b`). */
data class GameApiInstanceOptions(val apiName: String) {
    companion object {
        fun fromArgs(args: Array<String>): GameApiInstanceOptions {
            var apiName = "api-a"
            for (i in 0 until args.size - 1) {
                if (args[i] == "--api") {
                    apiName = args[i + 1]
                }
            }
            return GameApiInstanceOptions(apiName)
        }
    }
}
