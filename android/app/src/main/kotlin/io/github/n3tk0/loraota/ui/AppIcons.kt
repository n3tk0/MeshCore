package io.github.n3tk0.loraota.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathFillType
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.unit.dp

// Path data from Google's Material Icons (Apache License 2.0). Only material-icons-core is a dependency,
// so the few icons the core set lacks are defined here (24dp viewport each).
object AppIcons {
    /** "system_update": a phone with a download arrow (firmware). */
    val SystemUpdate: ImageVector by lazy {
        icon("SystemUpdate") {
            moveTo(17f, 1.01f)
            lineTo(7f, 1f)
            curveToRelative(-1.1f, 0f, -2f, 0.9f, -2f, 2f)
            verticalLineToRelative(18f)
            curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
            horizontalLineToRelative(10f)
            curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
            verticalLineTo(3f)
            curveToRelative(0f, -1.1f, -0.9f, -1.99f, -2f, -1.99f)
            close()
            moveTo(17f, 19f)
            horizontalLineTo(7f)
            verticalLineTo(5f)
            horizontalLineToRelative(10f)
            verticalLineToRelative(14f)
            close()
            moveTo(16f, 13f)
            horizontalLineToRelative(-3f)
            verticalLineTo(8f)
            horizontalLineToRelative(-2f)
            verticalLineToRelative(5f)
            horizontalLineTo(8f)
            lineToRelative(4f, 4f)
            lineToRelative(4f, -4f)
            close()
        }
    }

    /** "terminal": a console window with a prompt (tools). */
    val Terminal: ImageVector by lazy {
        icon("Terminal") {
            moveTo(20f, 4f)
            horizontalLineTo(4f)
            curveTo(2.9f, 4f, 2f, 4.9f, 2f, 6f)
            verticalLineToRelative(12f)
            curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
            horizontalLineToRelative(16f)
            curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
            verticalLineTo(6f)
            curveTo(22f, 4.9f, 21.1f, 4f, 20f, 4f)
            close()
            moveTo(7.5f, 16.5f)
            lineToRelative(-1.41f, -1.41f)
            lineTo(8.67f, 12.5f)
            lineTo(6.09f, 9.91f)
            lineTo(7.5f, 8.5f)
            lineToRelative(4f, 4f)
            lineToRelative(-4f, 4f)
            close()
            moveTo(18f, 16f)
            horizontalLineToRelative(-6f)
            verticalLineToRelative(-2f)
            horizontalLineToRelative(6f)
            verticalLineToRelative(2f)
            close()
        }
    }

    /** "cloud_off": a crossed-out cloud (offline). */
    val CloudOff: ImageVector by lazy {
        icon("CloudOff") {
            moveTo(19.35f, 10.04f)
            curveTo(18.67f, 6.59f, 15.64f, 4f, 12f, 4f)
            curveToRelative(-1.48f, 0f, -2.85f, 0.43f, -4.01f, 1.17f)
            lineToRelative(1.46f, 1.46f)
            curveTo(10.21f, 6.23f, 11.08f, 6f, 12f, 6f)
            curveToRelative(3.04f, 0f, 5.5f, 2.46f, 5.5f, 5.5f)
            verticalLineToRelative(0.5f)
            horizontalLineTo(19f)
            curveToRelative(1.66f, 0f, 3f, 1.34f, 3f, 3f)
            curveToRelative(0f, 1.13f, -0.64f, 2.11f, -1.56f, 2.62f)
            lineToRelative(1.45f, 1.45f)
            curveTo(23.16f, 18.16f, 24f, 16.68f, 24f, 15f)
            curveToRelative(0f, -2.64f, -2.05f, -4.78f, -4.65f, -4.96f)
            close()
            moveTo(3f, 5.27f)
            lineToRelative(2.75f, 2.74f)
            curveTo(2.56f, 8.15f, 0f, 10.77f, 0f, 14f)
            curveToRelative(0f, 3.31f, 2.69f, 6f, 6f, 6f)
            horizontalLineToRelative(11.73f)
            lineToRelative(2f, 2f)
            lineTo(21f, 20.73f)
            lineTo(4.27f, 4f)
            lineTo(3f, 5.27f)
            close()
            moveTo(7.73f, 10f)
            lineToRelative(8f, 8f)
            horizontalLineTo(6f)
            curveToRelative(-2.21f, 0f, -4f, -1.79f, -4f, -4f)
            reflectiveCurveToRelative(1.79f, -4f, 4f, -4f)
            horizontalLineToRelative(1.73f)
            close()
        }
    }

    private fun icon(name: String, pathBuilder: androidx.compose.ui.graphics.vector.PathBuilder.() -> Unit): ImageVector =
        ImageVector.Builder(
            name = name,
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f,
        ).path(fill = SolidColor(Color.Black), pathFillType = PathFillType.EvenOdd, pathBuilder = pathBuilder).build()
}
