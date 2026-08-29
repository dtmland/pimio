import QtQuick
import QtQuick.Controls

Item {
    id: controller

    objectName: "scrollController"

    property var grid: null
    property var window: null
    property bool browsingContextActive: true
    property real scrollSpeed: 2.0

    readonly property bool canScroll: grid !== null
                                      && grid.maximumContentY() > grid.minimumContentY()
    readonly property real maximumTilesPerTick: 0.12
    readonly property int tickIntervalMs: 16
    property real handleOffset: 0

    function jumpToStart() {
        if (!grid)
            return
        grid.contentY = grid.minimumContentY()
        grid.updateVisibleRange()
        if (window && typeof window.restoreGridFocus === "function")
            window.restoreGridFocus()
    }

    function jumpToEnd() {
        if (!grid)
            return
        grid.contentY = grid.maximumContentY()
        grid.updateVisibleRange()
        if (window && typeof window.restoreGridFocus === "function")
            window.restoreGridFocus()
    }

    function scrollFromDisplacement(displacement) {
        if (!canScroll)
            return
        const bounded = Math.max(-1, Math.min(1, displacement))
        const deadZone = 0.06
        if (Math.abs(bounded) <= deadZone)
            return
        const velocity = (Math.abs(bounded) - deadZone) / (1 - deadZone)
        const distance = Math.sign(bounded) * velocity
                * grid.cellHeight * maximumTilesPerTick * scrollSpeed
        grid.contentY = grid.boundedContentY(grid.contentY + distance)
        grid.updateVisibleRange()
    }

    function scrollFromHandle() {
        const halfTravel = Math.max(1, (scrollTrack.height - scrollHandle.height) / 2)
        const displacement = handleOffset / halfTravel
        scrollFromDisplacement(displacement)
    }

    function returnHandleToCenter() {
        returnAnimation.stop()
        returnAnimation.start()
    }

    ToolButton {
        id: jumpToStartButton
        objectName: "jumpToStartButton"
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: width
        text: "▲"
        enabled: controller.canScroll
                 && grid !== null
                 && grid.contentY > grid.minimumContentY()
        Accessible.name: qsTr("Jump to beginning")
        onClicked: controller.jumpToStart()
    }

    Item {
        id: scrollTrack
        objectName: "scrollControllerTrack"
        anchors {
            top: jumpToStartButton.bottom
            bottom: jumpToEndButton.top
            left: parent.left
            right: parent.right
            topMargin: 4
            bottomMargin: 4
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 4
            height: parent.height
            radius: 2
            color: "#d0d0d0"
        }

        Rectangle {
            id: scrollHandle
            objectName: "scrollControllerHandle"
            readonly property real restingY: (scrollTrack.height - height) / 2
            x: 2
            y: restingY + controller.handleOffset
            width: scrollTrack.width - 4
            height: Math.min(48, Math.max(28, scrollTrack.height / 5))
            radius: 5
            color: handleDrag.active ? "#707070" : "#909090"
            border.color: "#555555"

            DragHandler {
                id: handleDrag
                target: null
                xAxis.enabled: false
                enabled: controller.canScroll && controller.browsingContextActive
                onActiveTranslationChanged: {
                    const halfTravel = Math.max(
                            0, (scrollTrack.height - scrollHandle.height) / 2)
                    controller.handleOffset = Math.max(
                            -halfTravel, Math.min(halfTravel, activeTranslation.y))
                }
                onActiveChanged: {
                    if (active) {
                        returnAnimation.stop()
                        scrollTimer.start()
                    } else {
                        scrollTimer.stop()
                        controller.returnHandleToCenter()
                        if (controller.window && typeof controller.window.restoreGridFocus === "function")
                            controller.window.restoreGridFocus()
                    }
                }
            }
        }
    }

    ToolButton {
        id: jumpToEndButton
        objectName: "jumpToEndButton"
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: width
        text: "▼"
        enabled: controller.canScroll
                 && grid !== null
                 && grid.contentY < grid.maximumContentY()
        Accessible.name: qsTr("Jump to end")
        onClicked: controller.jumpToEnd()
    }

    Timer {
        id: scrollTimer
        interval: controller.tickIntervalMs
        repeat: true
        onTriggered: controller.scrollFromHandle()
    }

    NumberAnimation {
        id: returnAnimation
        target: controller
        property: "handleOffset"
        to: 0
        duration: 160
        easing.type: Easing.OutCubic
    }
}
