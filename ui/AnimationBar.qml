import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    readonly property var controller: viewer.animationController
    readonly property bool hasAnimation: controller ? controller.active : false
    readonly property bool isAnimated: controller ? controller.isAnimated : false
    readonly property bool isPlaying: controller ? controller.playing : false
    readonly property int currentFrame: controller ? controller.currentFrame : 0
    readonly property int frameCount: controller ? controller.frameCount : 0
    readonly property real speed: controller ? controller.speed : 1.0
    readonly property bool isLoop: controller ? controller.loop : true
    readonly property string label: controller ? controller.frameLabel : ""

    visible: hasAnimation
    implicitWidth: Math.min(layout.implicitWidth + Theme.gapMd * 2, parent ? parent.width - Theme.gapMd * 2 : 500)
    implicitHeight: Theme.controlHeight + Theme.gapXs * 2
    radius: Theme.radiusCard
    color: Theme.surface
    border.color: Theme.border
    border.width: Theme.borderWidth

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Theme.gapMd
        anchors.rightMargin: Theme.gapMd
        spacing: Theme.gapSm

        // Play / Pause Button (Only for animated formats)
        Rectangle {
            id: playPauseBtn
            visible: root.isAnimated
            implicitWidth: Theme.controlHeight - Theme.gapXs
            implicitHeight: Theme.controlHeight - Theme.gapXs
            radius: Theme.radiusSmall
            color: playPauseHover.hovered ? Theme.hover : "transparent"

            Canvas {
                id: playIconCanvas
                anchors.centerIn: parent
                width: 14; height: 14
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = Theme.text
                    if (root.isPlaying) {
                        ctx.fillRect(2, 1, 3, 12)
                        ctx.fillRect(9, 1, 3, 12)
                    } else {
                        ctx.beginPath()
                        ctx.moveTo(3, 1)
                        ctx.lineTo(13, 7)
                        ctx.lineTo(3, 13)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
            }

            Connections {
                target: root
                function onIsPlayingChanged() { playIconCanvas.requestPaint() }
            }

            HoverHandler { id: playPauseHover }
            TapHandler {
                onTapped: if (root.controller) root.controller.togglePlayPause()
            }
        }

        // Previous Frame / Page Button
        Rectangle {
            id: prevBtn
            implicitWidth: Theme.controlHeight - Theme.gapXs
            implicitHeight: Theme.controlHeight - Theme.gapXs
            radius: Theme.radiusSmall
            color: prevHover.hovered ? Theme.hover : "transparent"

            Text {
                anchors.centerIn: parent
                text: "◀"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }

            HoverHandler { id: prevHover }
            TapHandler {
                onTapped: if (root.controller) root.controller.previousFrame()
            }
        }

        // Next Frame / Page Button
        Rectangle {
            id: nextBtn
            implicitWidth: Theme.controlHeight - Theme.gapXs
            implicitHeight: Theme.controlHeight - Theme.gapXs
            radius: Theme.radiusSmall
            color: nextHover.hovered ? Theme.hover : "transparent"

            Text {
                anchors.centerIn: parent
                text: "▶"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }

            HoverHandler { id: nextHover }
            TapHandler {
                onTapped: if (root.controller) root.controller.nextFrame()
            }
        }

        // Frame / Page Label
        Text {
            id: labelText
            text: root.label
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            font.bold: true
            Layout.minimumWidth: 80
            horizontalAlignment: Text.AlignHCenter
        }

        // Frame Scrubber Slider
        Slider {
            id: scrubber
            Layout.fillWidth: true
            from: 0
            to: Math.max(0, root.frameCount - 1)
            stepSize: 1
            value: root.currentFrame
            live: true

            background: Rectangle {
                x: scrubber.leftPadding
                y: scrubber.topPadding + scrubber.availableHeight / 2 - height / 2
                implicitWidth: 160
                implicitHeight: 4
                width: scrubber.availableWidth
                height: implicitHeight
                radius: 2
                color: Theme.border

                Rectangle {
                    width: scrubber.visualPosition * parent.width
                    height: parent.height
                    color: Theme.accent
                    radius: 2
                }
            }

            handle: Rectangle {
                x: scrubber.leftPadding + scrubber.visualPosition * (scrubber.availableWidth - width)
                y: scrubber.topPadding + scrubber.availableHeight / 2 - height / 2
                implicitWidth: 14
                implicitHeight: 14
                radius: 7
                color: scrubber.pressed ? Theme.accent : Theme.text
                border.color: Theme.surface
                border.width: 1
            }

            onMoved: {
                if (root.controller) {
                    root.controller.jumpToFrame(Math.round(value))
                }
            }
        }

        // Speed Selector Button (for animated formats)
        Rectangle {
            id: speedBtn
            visible: root.isAnimated
            implicitWidth: 48
            implicitHeight: Theme.controlHeight - Theme.gapXs
            radius: Theme.radiusSmall
            color: speedHover.hovered ? Theme.hover : "transparent"

            Text {
                anchors.centerIn: parent
                text: root.speed.toFixed(root.speed === Math.floor(root.speed) ? 1 : 2) + "x"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textCaption
                font.bold: true
            }

            HoverHandler { id: speedHover }
            TapHandler {
                onTapped: {
                    if (!root.controller) return
                    var speeds = [0.25, 0.5, 1.0, 1.5, 2.0]
                    var current = root.speed
                    var next = 1.0
                    for (var i = 0; i < speeds.length; ++i) {
                        if (Math.abs(speeds[i] - current) < 0.05) {
                            next = speeds[(i + 1) % speeds.length]
                            break
                        }
                    }
                    root.controller.setSpeed(next)
                }
            }
        }

        // Loop Toggle Button (for animated formats)
        Rectangle {
            id: loopBtn
            visible: root.isAnimated
            implicitWidth: 36
            implicitHeight: Theme.controlHeight - Theme.gapXs
            radius: Theme.radiusSmall
            color: root.isLoop ? Qt.alpha(Theme.accent, 0.2) : loopHover.hovered ? Theme.hover : "transparent"
            border.color: root.isLoop ? Theme.accent : "transparent"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "🔁"
                font.pixelSize: Theme.textCaption
                opacity: root.isLoop ? 1.0 : 0.5
            }

            HoverHandler { id: loopHover }
            TapHandler {
                onTapped: if (root.controller) root.controller.setLoop(!root.isLoop)
            }
        }
    }
}
