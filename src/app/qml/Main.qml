import QtQuick
import QtQuick.Window
import QtQuick.Controls

Window {
    id: root

    property var mediaModel: typeof mediaLibraryModel === "undefined"
                             ? null : mediaLibraryModel
    // Settings are registered by pimio::app::loadMainQml() before this file
    // is loaded, so this is only null in a test that deliberately removed
    // them; every read below falls back to the same default the C++ side
    // uses, so the window still works.
    property var settings: typeof appSettings === "undefined" ? null : appSettings
    // Set by LibrarySession when a real library is open; null in a test or a
    // build with no durable store, where nothing is ever scanning.
    property var activity: typeof libraryActivity === "undefined" ? null : libraryActivity
    property bool scanning: activity ? activity.scanning : false
    property int indexedCount: activity ? activity.indexedCount : 0
    property int selectedIndex: -1
    readonly property bool browsingContextActive: !detail.visible && !settingsDialog.visible

    readonly property int tileSize: settings ? settings.tileSize : 176
    readonly property real scrollSpeed: settings ? settings.scrollSpeed : 2.0
    readonly property bool scrollAcceleration: settings ? settings.scrollAcceleration : true
    readonly property bool keyRepeatAcceleration: settings ? settings.keyRepeatAcceleration : true
    readonly property bool showTileDiagnostics: settings ? settings.showTileDiagnostics : false

    objectName: "pimioMainWindow"
    width: 1024
    height: 720
    title: qsTr("pimio")
    visible: true

    onActiveChanged: if (active) restoreGridFocus()
    Component.onCompleted: restoreGridFocus()

    // How far one held navigation key jumps. A single press always moves one
    // step; while the key repeats the step grows, so a long hold crosses a
    // large library without the user releasing the key, and it stops growing
    // at a speed a person can still follow.
    readonly property int maximumKeyStep: 8
    property int keyRepeatCount: 0
    property int lastNavigationKey: 0

    function navigationStep(event) {
        if (event.key !== lastNavigationKey) {
            lastNavigationKey = event.key
            keyRepeatCount = 0
        }
        if (!event.isAutoRepeat) {
            keyRepeatCount = 0
            return 1
        }
        keyRepeatCount = keyRepeatCount + 1
        if (!keyRepeatAcceleration)
            return 1
        // One extra step every four repeats: about a second of holding the
        // key before it moves noticeably faster than a single press.
        return Math.min(maximumKeyStep, 1 + Math.floor(keyRepeatCount / 4))
    }

    function endKeyRepeat() {
        keyRepeatCount = 0
        lastNavigationKey = 0
    }

    function restoreGridFocus() {
        Qt.callLater(forceGridFocusIfBrowsing)
    }

    function forceGridFocusIfBrowsing() {
        if (browsingContextActive)
            grid.forceActiveFocus()
    }

    function showDetail(index) {
        if (!mediaModel || index < 0 || index >= grid.count)
            return

        selectedIndex = index
        grid.currentIndex = index
        mediaModel.requestThumbnail(index)
        const item = mediaModel.itemAt(index)
        detail.mediaId = item.mediaId
        detail.absolutePath = item.absolutePath
        detail.captureTime = item.captureTimeString
        detail.mediaKind = item.mediaKind
        detail.thumbnailStatus = item.thumbnailStatus
        detail.thumbnailSource = "image://thumbnail/" + detail.mediaId
        detail.visible = true
        detail.forceActiveFocus()
    }

    // Moves the preview to another item. The step is in rows of the grid, so
    // the preview always follows the order the grid is showing.
    function stepDetail(delta) {
        if (!mediaModel || selectedIndex < 0)
            return
        const next = Math.max(0, Math.min(grid.count - 1, selectedIndex + delta))
        if (next !== selectedIndex)
            showDetail(next)
    }

    function refreshDetailThumbnail() {
        if (!detail.visible || !mediaModel || selectedIndex < 0)
            return

        const item = mediaModel.itemAt(selectedIndex)
        detail.thumbnailStatus = item.thumbnailStatus
    }

    function sortOptions() {
        if (!settings)
            return []
        const keys = settings.sortKeyValues()
        let entries = []
        for (let i = 0; i < keys.length; ++i)
            entries.push({ key: keys[i], label: settings.sortKeyLabelFor(keys[i]) })
        return entries
    }

    function indexOfSortKey(key) {
        if (!settings)
            return 0
        const keys = settings.sortKeyValues()
        for (let i = 0; i < keys.length; ++i) {
            if (keys[i] === key)
                return i
        }
        return 0
    }

    Connections {
        target: root.mediaModel
        function onDataChanged() {
            root.refreshDetailThumbnail()
        }
    }

    // Top bar
    TopBar {
        id: toolbar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 48
        settings: root.settings
        scanning: root.scanning
        indexedCount: root.indexedCount
        tileSize: root.tileSize
        minimumTileSize: root.settings ? root.settings.minimumTileSize : 96
        maximumTileSize: root.settings ? root.settings.maximumTileSize : 256
        sortOptions: root.sortOptions()
        sortKey: root.settings ? root.settings.sortKey : 0
        sortDescending: root.settings ? root.settings.sortDescending : false
        settingsDialog: settingsDialog
    }

    // Media grid
    GridView {
        id: grid
        objectName: "mediaGrid"
        anchors {
            top: toolbar.bottom
            left: parent.left
            right: scrollController.left
            bottom: parent.bottom
            margins: 8
        }

        cellWidth: root.tileSize
        cellHeight: root.tileSize
        clip: true
        cacheBuffer: cellHeight * 4
        model: root.mediaModel
        focus: root.browsingContextActive
        // Arrow keys are handled explicitly below so a held key can
        // accelerate; the built-in handling has one fixed step.
        keyNavigationEnabled: false
        highlightMoveDuration: 0

        property real lastWheelMs: 0
        property int wheelStreak: 0

        readonly property int columns: Math.max(1, Math.floor(width / cellWidth))

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.WithinBounds
            onTapped: root.restoreGridFocus()
        }

        highlight: Rectangle {
            color: "transparent"
            border.color: "#5a9fd4"
            border.width: 2
            radius: 4
            visible: grid.count > 0
        }

        Keys.onPressed: function(event) {
            const step = root.navigationStep(event)
            switch (event.key) {
            case Qt.Key_Up:
                grid.moveSelectionWithViewport(-grid.columns * step)
                break
            case Qt.Key_Down:
                grid.moveSelectionWithViewport(grid.columns * step)
                break
            case Qt.Key_Left:
                grid.moveSelection(-step)
                break
            case Qt.Key_Right:
                grid.moveSelection(step)
                break
            case Qt.Key_PageUp:
                grid.moveSelectionWithViewport(-grid.columns * grid.rowsPerPage() * step)
                break
            case Qt.Key_PageDown:
                grid.moveSelectionWithViewport(grid.columns * grid.rowsPerPage() * step)
                break
            case Qt.Key_Home:
                grid.moveSelection(-grid.count)
                break
            case Qt.Key_End:
                grid.moveSelection(grid.count)
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                root.showDetail(grid.currentIndex)
                break
            default:
                return
            }
            event.accepted = true
        }

        Keys.onReleased: function(event) {
            if (!event.isAutoRepeat)
                root.endKeyRepeat()
        }

        function rowsPerPage() {
            return Math.max(1, Math.floor(height / cellHeight))
        }

        function minimumContentY() {
            return originY
        }

        function maximumContentY() {
            return Math.max(originY, originY + contentHeight - height)
        }

        function boundedContentY(value) {
            return Math.max(minimumContentY(), Math.min(maximumContentY(), value))
        }

        function moveSelection(delta) {
            moveSelectionInternal(delta, false)
        }

        function moveSelectionWithViewport(delta) {
            moveSelectionInternal(delta, true)
        }

        function moveSelectionInternal(delta, scrollViewport) {
            if (count === 0)
                return
            const start = currentIndex < 0 ? 0 : currentIndex
            const next = Math.max(0, Math.min(count - 1, start + delta))
            const startRow = Math.floor(start / columns)
            const nextRow = Math.floor(next / columns)
            currentIndex = next
            root.selectedIndex = next
            // Vertical navigation moves the viewport by the same rows as the
            // selection. The selected tile therefore keeps its place on
            // screen instead of walking through an invisible viewport cursor
            // before the content starts to move.
            if (scrollViewport)
                contentY = boundedContentY(contentY + (nextRow - startRow) * cellHeight)
            positionViewAtIndex(next, GridView.Contain)
            updateVisibleRange()
        }

        // Wheel scrolling at the speed the user chose, with an optional
        // ramp-up. Qt's own Flickable wheel handling has one fixed step,
        // which on a dense grid of tiles is slow enough to be a complaint.
        //
        // Called from the wheel handler below, and directly by tests, which
        // is why the timestamp can be supplied.
        function scrollByWheel(angleDeltaY, pixelDeltaY, timestampMs) {
            if (count === 0)
                return

            const now = timestampMs === undefined ? Date.now() : timestampMs
            // A gap longer than this ends the gesture, so a slow, deliberate
            // scroll never inherits the speed of the last fast one.
            if (now - lastWheelMs > 250)
                wheelStreak = 0
            else
                wheelStreak = wheelStreak + 1
            lastWheelMs = now

            const factor = root.scrollAcceleration
                    ? Math.min(4.0, 1.0 + wheelStreak * 0.25) : 1.0

            // A high-resolution device (touchpad, precision wheel) reports
            // pixels and is scaled directly; a notched wheel reports 120ths
            // of a degree, and one notch moves half a tile before the user's
            // speed setting is applied.
            const distance = pixelDeltaY !== 0
                    ? pixelDeltaY * root.scrollSpeed * factor
                    : (angleDeltaY / 120) * cellHeight * 0.5 * root.scrollSpeed * factor

            contentY = boundedContentY(contentY - distance)
            updateVisibleRange()
        }

        delegate: Rectangle {
            width: grid.cellWidth - 4
            height: grid.cellHeight - 4
            color: "#3c3c3c"
            radius: 4

            TapHandler {
                onTapped: root.showDetail(index)
            }

            // Thumbnail or placeholder
            Image {
                objectName: "gridThumbnail"
                anchors.fill: parent
                anchors.margins: 2
                cache: false
                fillMode: Image.PreserveAspectCrop
                visible: model.thumbnailStatus === 2 // ThumbnailStatus::Ready
                source: model.thumbnailStatus === 2
                        ? "image://thumbnail/" + model.mediaId
                        : ""

                // The model says this row has a thumbnail but the provider
                // could not serve it. Ask the model to render it again rather
                // than leaving a grey tile that nothing would ever fix. Once
                // per delegate: a file that genuinely cannot be rendered must
                // not turn into an endless re-request loop.
                property bool retried: false
                onSourceChanged: retried = false
                onStatusChanged: {
                    if (status === Image.Error && !retried && root.mediaModel
                            && typeof root.mediaModel.refreshThumbnail === "function") {
                        retried = true
                        root.mediaModel.refreshThumbnail(index)
                    }
                }
            }

            // Placeholder while loading
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                color: "transparent"
                visible: model.thumbnailStatus !== 2
                Text {
                    anchors.centerIn: parent
                    color: "#888888"
                    font.pixelSize: 11
                    text: model.thumbnailStatus === 3 ? qsTr("Error") : qsTr("…")
                }
            }

            // Video badge
            Rectangle {
                anchors { bottom: parent.bottom; right: parent.right; margins: 4 }
                width: 36; height: 18; radius: 3
                color: "#cc000000"
                visible: model.mediaKind === 2 // MediaKind::Video
                Text {
                    anchors.centerIn: parent
                    color: "#ffffff"
                    font.pixelSize: 10
                    text: qsTr("Video")
                }
            }

            // Session-only diagnostics: which row this is and what state its
            // thumbnail is in, so a report about "the third tile" can be
            // matched to a row without counting tiles in a screenshot.
            Rectangle {
                objectName: "tileDiagnostics"
                anchors { top: parent.top; left: parent.left; margins: 4 }
                width: diagnosticsLabel.implicitWidth + 6
                height: diagnosticsLabel.implicitHeight + 4
                radius: 3
                color: "#cc000000"
                visible: root.showTileDiagnostics
                Text {
                    id: diagnosticsLabel
                    anchors.centerIn: parent
                    color: "#ffffff"
                    font.pixelSize: 10
                    text: index + " · " + model.thumbnailStatus
                }
            }
        }

        // Notify the model when the visible range changes so it can manage
        // thumbnail requests for the on-screen window plus prefetch margin.
        onContentYChanged: updateVisibleRange()
        onOriginYChanged: Qt.callLater(updateVisibleRange)
        onContentHeightChanged: Qt.callLater(clampToContent)
        onWidthChanged: Qt.callLater(refreshGeometry)
        onHeightChanged: Qt.callLater(refreshGeometry)
        onCellWidthChanged: Qt.callLater(refreshGeometry)
        onCellHeightChanged: Qt.callLater(refreshGeometry)
        onCountChanged: Qt.callLater(refreshGeometry)
        Component.onCompleted: Qt.callLater(refreshGeometry)

        function clampToContent() {
            contentY = boundedContentY(contentY)
            updateVisibleRange()
        }

        function refreshGeometry() {
            forceLayout()
            clampToContent()
        }

        function updateVisibleRange() {
            const columnCount = Math.max(1, Math.floor(width / cellWidth))
            // Item views may move their logical origin when rows are inserted
            // or their geometry changes. contentY is in that shifted
            // coordinate system; row numbers start at originY.
            const relativeY = Math.max(0, contentY - originY)
            const firstRow = Math.floor(relativeY / cellHeight)
            const lastRow = Math.floor(Math.max(0, relativeY + height - 1) / cellHeight)
            const first = Math.min(count - 1, firstRow * columnCount)
            const last = Math.min(count - 1, (lastRow + 1) * columnCount - 1)
            if (model && typeof model.setVisibleRange === "function") {
                model.setVisibleRange(Math.max(0, first), Math.max(0, last))
            }
        }
    }

    // Picasa-style scroll controller: the center handle is a velocity control,
    // not a position indicator. Pull it away from center to scroll, farther for
    // faster movement; releasing it returns it to rest.
    Item {
        id: scrollController
        objectName: "scrollController"
        anchors {
            top: toolbar.bottom
            right: parent.right
            bottom: parent.bottom
            topMargin: 8
            rightMargin: 8
            bottomMargin: 8
        }
        width: 28

        readonly property bool canScroll: grid.maximumContentY() > grid.minimumContentY()
        readonly property real maximumTilesPerTick: 0.12
        readonly property int tickIntervalMs: 16
        property real handleOffset: 0

        function jumpToStart() {
            grid.contentY = grid.minimumContentY()
            grid.updateVisibleRange()
            root.restoreGridFocus()
        }

        function jumpToEnd() {
            grid.contentY = grid.maximumContentY()
            grid.updateVisibleRange()
            root.restoreGridFocus()
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
                    * grid.cellHeight * maximumTilesPerTick * root.scrollSpeed
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
            text: "\u25b2"
            enabled: scrollController.canScroll
                     && grid.contentY > grid.minimumContentY()
            Accessible.name: qsTr("Jump to beginning")
            onClicked: scrollController.jumpToStart()
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
                y: restingY + scrollController.handleOffset
                width: scrollTrack.width - 4
                height: Math.min(48, Math.max(28, scrollTrack.height / 5))
                radius: 5
                color: handleDrag.active ? "#707070" : "#909090"
                border.color: "#555555"

                DragHandler {
                    id: handleDrag
                    target: null
                    xAxis.enabled: false
                    enabled: scrollController.canScroll && root.browsingContextActive
                    onActiveTranslationChanged: {
                        const halfTravel = Math.max(
                                0, (scrollTrack.height - scrollHandle.height) / 2)
                        scrollController.handleOffset = Math.max(
                                -halfTravel, Math.min(halfTravel, activeTranslation.y))
                    }
                    onActiveChanged: {
                        if (active) {
                            returnAnimation.stop()
                            scrollTimer.start()
                        } else {
                            scrollTimer.stop()
                            scrollController.returnHandleToCenter()
                            root.restoreGridFocus()
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
            text: "\u25bc"
            enabled: scrollController.canScroll
                     && grid.contentY < grid.maximumContentY()
            Accessible.name: qsTr("Jump to end")
            onClicked: scrollController.jumpToEnd()
        }

        Timer {
            id: scrollTimer
            interval: scrollController.tickIntervalMs
            repeat: true
            onTriggered: scrollController.scrollFromHandle()
        }

        NumberAnimation {
            id: returnAnimation
            target: scrollController
            property: "handleOffset"
            to: 0
            duration: 160
            easing.type: Easing.OutCubic
        }
    }

    // Wheel events reach this before the GridView's own flick handling, so
    // the scroll distance is the one computed above. It accepts no buttons,
    // which leaves clicks and taps to the delegates underneath.
    MouseArea {
        objectName: "gridWheelArea"
        anchors.fill: grid
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
        enabled: root.browsingContextActive
        onWheel: function(wheel) {
            grid.scrollByWheel(wheel.angleDelta.y, wheel.pixelDelta.y)
            wheel.accepted = true
        }
    }

    // Empty-library placeholder
    // Shown while a scan has not produced anything to display yet, so an
    // empty window during a long first scan reads as "working", not "broken".
    Column {
        objectName: "scanningPlaceholder"
        anchors.centerIn: grid
        visible: grid.count === 0 && root.scanning
        spacing: 12

        BusyIndicator {
            objectName: "scanningPlaceholderIndicator"
            anchors.horizontalCenter: parent.horizontalCenter
            running: parent.visible
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#888888"
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Scanning your library…\n%1 found so far").arg(root.indexedCount)
        }
    }

    Column {
        objectName: "emptyLibraryPlaceholder"
        anchors.centerIn: grid
        visible: grid.count === 0 && !root.scanning
        spacing: 12

        // Camera glyph drawn from primitives: a color-emoji glyph ("📷")
        // renders as an empty box on Linux systems without an emoji font,
        // so the icon is drawn instead of typed.
        Item {
            objectName: "emptyLibraryIcon"
            width: 72
            height: 60
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle { // viewfinder bump
                x: 12; y: 0
                width: 22; height: 14
                radius: 4
                color: "#666666"
            }
            Rectangle { // body
                y: 8
                width: parent.width
                height: parent.height - 8
                radius: 8
                color: "#666666"

                Rectangle { // lens outer ring
                    anchors.centerIn: parent
                    width: 32; height: 32; radius: 16
                    color: "#3c3c3c"
                    border.color: "#999999"
                    border.width: 3

                    Rectangle { // lens inner
                        anchors.centerIn: parent
                        width: 12; height: 12; radius: 6
                        color: "#999999"
                    }
                }
                Rectangle { // flash dot
                    x: parent.width - 18; y: 8
                    width: 8; height: 8; radius: 4
                    color: "#999999"
                }
            }

        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#888888"
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No media yet\nLaunch with --library <folder> to scan and browse a library.")
        }
    }

    DetailView {
        id: detail
        anchors.fill: parent
        z: 10
        keyRepeatAcceleration: root.keyRepeatAcceleration
        onCloseRequested: {
            visible = false
            root.endKeyRepeat()
            root.restoreGridFocus()
        }
        onStepRequested: function(delta) { root.stepDetail(delta) }
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, root.width - 48)
        settings: root.settings
        onClosed: root.restoreGridFocus()
    }
}
