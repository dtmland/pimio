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
    property int selectedIndex: -1

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
    Rectangle {
        id: toolbar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 48
        color: "#2b2b2b"

        Text {
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            color: "#ffffff"
            font.pixelSize: 16
            text: qsTr("pimio %1").arg(Qt.application.version)
            objectName: "placeholderLabel"
        }

        // The two controls a user reaches for constantly sit in the bar
        // itself; everything else is one click away behind the button.
        Row {
            anchors {
                right: parent.right
                rightMargin: 12
                verticalCenter: parent.verticalCenter
            }
            spacing: 8

            ComboBox {
                objectName: "sortComboBox"
                width: 150
                anchors.verticalCenter: parent.verticalCenter
                textRole: "label"
                valueRole: "key"
                model: root.sortOptions()
                currentIndex: root.settings ? root.indexOfSortKey(root.settings.sortKey) : 0
                onActivated: if (root.settings)
                                 root.settings.sortKey = valueAt(currentIndex)
            }

            ToolButton {
                objectName: "sortDirectionButton"
                anchors.verticalCenter: parent.verticalCenter
                // An arrow rather than a word: the button sits next to the
                // field it applies to, and it carries an accessible name.
                text: root.settings && root.settings.sortDescending ? "\u2193" : "\u2191"
                Accessible.name: root.settings && root.settings.sortDescending
                                 ? qsTr("Sort descending") : qsTr("Sort ascending")
                onClicked: if (root.settings)
                               root.settings.sortDescending = !root.settings.sortDescending
            }

            Slider {
                objectName: "tileSizeSlider"
                width: 120
                anchors.verticalCenter: parent.verticalCenter
                from: root.settings ? root.settings.minimumTileSize : 96
                to: root.settings ? root.settings.maximumTileSize : 256
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: root.tileSize
                // Live while the handle moves, the way Picasa's thumbnail
                // slider behaved.
                onMoved: if (root.settings)
                             root.settings.tileSize = Math.round(value)
            }

            ToolButton {
                objectName: "settingsButton"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Settings")
                onClicked: settingsDialog.open()
            }
        }
    }

    // Media grid
    GridView {
        id: grid
        objectName: "mediaGrid"
        anchors {
            top: toolbar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 8
        }

        cellWidth: root.tileSize
        cellHeight: root.tileSize
        clip: true
        cacheBuffer: cellHeight * 4
        model: root.mediaModel
        focus: !detail.visible
        // Arrow keys are handled explicitly below so a held key can
        // accelerate; the built-in handling has one fixed step.
        keyNavigationEnabled: false
        highlightMoveDuration: 0

        property real lastWheelMs: 0
        property int wheelStreak: 0

        readonly property int columns: Math.max(1, Math.floor(width / cellWidth))

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
                grid.moveSelection(-grid.columns * step)
                break
            case Qt.Key_Down:
                grid.moveSelection(grid.columns * step)
                break
            case Qt.Key_Left:
                grid.moveSelection(-step)
                break
            case Qt.Key_Right:
                grid.moveSelection(step)
                break
            case Qt.Key_PageUp:
                grid.moveSelection(-grid.columns * grid.rowsPerPage() * step)
                break
            case Qt.Key_PageDown:
                grid.moveSelection(grid.columns * grid.rowsPerPage() * step)
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

        function moveSelection(delta) {
            if (count === 0)
                return
            const start = currentIndex < 0 ? 0 : currentIndex
            const next = Math.max(0, Math.min(count - 1, start + delta))
            currentIndex = next
            root.selectedIndex = next
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

            const maximum = Math.max(0, contentHeight - height)
            contentY = Math.max(0, Math.min(maximum, contentY - distance))
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
        onHeightChanged: updateVisibleRange()
        onCellHeightChanged: Qt.callLater(updateVisibleRange)
        onCountChanged: Qt.callLater(updateVisibleRange)
        Component.onCompleted: Qt.callLater(updateVisibleRange)

        function updateVisibleRange() {
            const columnCount = Math.max(1, Math.floor(width / cellWidth))
            const firstRow = Math.floor(Math.max(0, contentY) / cellHeight)
            const lastRow = Math.floor(Math.max(0, contentY + height - 1) / cellHeight)
            const first = Math.min(count - 1, firstRow * columnCount)
            const last = Math.min(count - 1, (lastRow + 1) * columnCount - 1)
            if (model && typeof model.setVisibleRange === "function") {
                model.setVisibleRange(Math.max(0, first), Math.max(0, last))
            }
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
        enabled: !detail.visible
        onWheel: function(wheel) {
            grid.scrollByWheel(wheel.angleDelta.y, wheel.pixelDelta.y)
            wheel.accepted = true
        }
    }

    // Empty-library placeholder
    Column {
        objectName: "emptyLibraryPlaceholder"
        anchors.centerIn: grid
        visible: grid.count === 0
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
            grid.forceActiveFocus()
        }
        onStepRequested: function(delta) { root.stepDetail(delta) }
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, root.width - 48)
        settings: root.settings
    }
}
