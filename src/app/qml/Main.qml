import QtQuick
import QtQuick.Window
import QtQuick.Controls

Window {
    id: root

    property var mediaModel: typeof mediaLibraryModel === "undefined"
                             ? null : mediaLibraryModel
    property int selectedIndex: -1

    objectName: "pimioMainWindow"
    width: 1024
    height: 720
    title: qsTr("pimio")
    visible: true

    function showDetail(index) {
        if (!mediaModel || index < 0 || index >= grid.count)
            return

        selectedIndex = index
        const item = mediaModel.itemAt(index)
        detail.mediaId = item.mediaId
        detail.absolutePath = item.absolutePath
        detail.captureTime = item.captureTimeString
        detail.mediaKind = item.mediaKind
        detail.thumbnailSource = "image://thumbnail/" + detail.mediaId
        detail.visible = true
        detail.forceActiveFocus()
    }

    // Top bar
    Rectangle {
        id: toolbar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 48
        color: "#2b2b2b"

        Text {
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 16
            text: qsTr("pimio %1").arg(Qt.application.version)
            objectName: "placeholderLabel"
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

        cellWidth: 176
        cellHeight: 176
        clip: true
        cacheBuffer: cellHeight * 4
        model: root.mediaModel

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
                anchors.fill: parent
                anchors.margins: 2
                fillMode: Image.PreserveAspectCrop
                visible: model.thumbnailStatus === 2 // ThumbnailStatus::Ready
                source: (model.thumbnailImage !== null && model.thumbnailStatus === 2)
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
        }

        // Notify the model when the visible range changes so it can manage
        // thumbnail requests for the on-screen window plus prefetch margin.
        onContentYChanged: updateVisibleRange()
        onHeightChanged: updateVisibleRange()
        onCountChanged: Qt.callLater(updateVisibleRange)
        Component.onCompleted: Qt.callLater(updateVisibleRange)

        function updateVisibleRange() {
            const columns = Math.max(1, Math.floor(width / cellWidth))
            const firstRow = Math.floor(Math.max(0, contentY) / cellHeight)
            const lastRow = Math.floor(Math.max(0, contentY + height - 1) / cellHeight)
            const first = Math.min(count - 1, firstRow * columns)
            const last = Math.min(count - 1, (lastRow + 1) * columns - 1)
            if (model && typeof model.setVisibleRange === "function") {
                model.setVisibleRange(Math.max(0, first), Math.max(0, last))
            }
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
            text: qsTr("No media yet — library folders can't be added in this build.\nThe library browser is still in development.")
        }
    }

    DetailView {
        id: detail
        anchors.fill: parent
        z: 10
        onCloseRequested: {
            visible = false
            root.selectedIndex = -1
            grid.forceActiveFocus()
        }
    }
}
