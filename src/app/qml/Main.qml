import QtQuick
import QtQuick.Window
import QtQuick.Controls

Window {
    id: root

    objectName: "pimioMainWindow"
    width: 1024
    height: 720
    title: qsTr("pimio")
    visible: true

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

        delegate: Rectangle {
            width: grid.cellWidth - 4
            height: grid.cellHeight - 4
            color: "#3c3c3c"
            radius: 4

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

        function updateVisibleRange() {
            const first = indexAt(1, contentY)
            const last  = indexAt(1, contentY + height - 1)
            if (model && typeof model.setVisibleRange === "function") {
                model.setVisibleRange(Math.max(0, first), last >= 0 ? last : count - 1)
            }
        }
    }

    // Empty-library placeholder
    Column {
        anchors.centerIn: grid
        visible: grid.count === 0
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#666666"
            font.pixelSize: 48
            text: "📷"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#888888"
            font.pixelSize: 16
            text: qsTr("No media — add a library folder to get started.")
        }
    }
}
