package io.github.n3tk0.loraota.github

/** One input of a workflow_dispatch: a choice (board lists), a boolean (parts) or a string. */
data class WorkflowInput(val name: String, val description: String, val type: String, val default: String,
                         val options: List<String>) {
    val isBoolean get() = type == "boolean"
    val defaultOn get() = default == "true"
}

/**
 * The inputs of a workflow file, read the plain way GitHub workflow files are written (two spaces an
 * indent, `options:` as a list of `- value`), so the app's boards and boxes are always those of the
 * workflow on GitHub.
 */
object WorkflowInputs {
    fun parse(yaml: String): List<WorkflowInput> {
        val lines = yaml.lines()
        var at = lines.indexOfFirst { it.trim() == "workflow_dispatch:" }
        if (at < 0) return emptyList()
        val base = indent(lines[at])
        at++
        // inputs: one level under workflow_dispatch
        while (at < lines.size && (blank(lines[at]) || indent(lines[at]) > base) && lines[at].trim() != "inputs:") at++
        if (at >= lines.size || indent(lines[at]) <= base) return emptyList()
        val inputsIndent = indent(lines[at])
        at++
        val out = ArrayList<WorkflowInput>()
        var nameIndent = -1
        var name: String? = null
        val props = HashMap<String, String>()
        val options = ArrayList<String>()
        var inOptions = false
        fun flush() {
            val n = name ?: return
            out.add(WorkflowInput(n, props["description"] ?: "", props["type"] ?: "string", props["default"] ?: "", options.toList()))
            name = null
            props.clear()
            options.clear()
            inOptions = false
        }
        while (at < lines.size) {
            val line = lines[at++]
            if (blank(line)) continue
            val ind = indent(line)
            if (ind <= inputsIndent) break
            val t = line.trim()
            if (nameIndent < 0) nameIndent = ind
            when {
                ind == nameIndent && t.endsWith(":") -> { flush(); name = t.removeSuffix(":") }
                inOptions && t.startsWith("- ") -> options.add(unquote(t.removePrefix("- ")))
                ind > nameIndent && ':' in t -> {
                    val key = t.substringBefore(':').trim()
                    val value = t.substringAfter(':').trim()
                    inOptions = key == "options" && value.isEmpty()
                    if (!inOptions) props[key] = unquote(value)
                }
            }
        }
        flush()
        return out
    }

    private fun blank(line: String) = line.isBlank() || line.trimStart().startsWith("#")

    private fun indent(line: String) = line.length - line.trimStart().length

    private fun unquote(v: String): String {
        val s = v.trim()
        return if (s.length >= 2 && (s[0] == '\'' || s[0] == '"') && s.last() == s[0]) s.substring(1, s.length - 1) else s
    }
}
