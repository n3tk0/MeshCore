package io.github.n3tk0.loraota.github

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class WorkflowInputsTest {
    /** The real workflow: the app shows its boards and boxes. */
    @Test
    fun readsBuildBoardsWorkflow() {
        val yaml = File("../../.github/workflows/build-boards.yml").readText()
        val inputs = WorkflowInputs.parse(yaml).associateBy { it.name }
        val repeater = inputs.getValue("repeater")
        assertEquals("choice", repeater.type)
        assertEquals("-", repeater.default)
        assertEquals("-", repeater.options.first())
        assertTrue("Xiao_nrf52_repeater" in repeater.options)
        assertTrue(repeater.options.none { it.startsWith("#") })
        assertTrue("Faketec_v5_companion_radio_ble" in inputs.getValue("companion").options)
        assertTrue(inputs.getValue("mailbox").isBoolean && inputs.getValue("mailbox").defaultOn)
        assertTrue(!inputs.getValue("display").defaultOn)
        assertEquals("string", inputs.getValue("request_id").type)
        assertEquals("", inputs.getValue("request_id").default)
        // a description with a colon in it
        assertTrue(inputs.getValue("group").description.startsWith("Група: "))
        assertTrue("jobs" !in inputs && "targets" !in inputs)
    }

    @Test
    fun plainFile() {
        val yaml = """
            on:
              workflow_dispatch:
                inputs:
                  board:
                    description: "Board"
                    type: choice
                    options:
                      - a
                      # comment
                      - 'b'
                  fast:
                    type: boolean
                    default: true
              push:
            jobs:
              x:
        """.trimIndent()
        val inputs = WorkflowInputs.parse(yaml)
        assertEquals(listOf("board", "fast"), inputs.map { it.name })
        assertEquals(listOf("a", "b"), inputs[0].options)
        assertEquals("Board", inputs[0].description)
        assertTrue(inputs[1].defaultOn)
    }
}
