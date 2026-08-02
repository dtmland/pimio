import QtQuick
import QtQuick.Controls

Rectangle {
    id: detail

    property string mediaId
    property string absolutePath
    property string captureTime
    property int mediaKind
    property int thumbnailStatus
    property url thumbnailSource
    signal closeRequested()

    objectName: "detailView"
    color: "#ee1b1b1b"
    visible: false
    focus: visible

    Keys.onEscapePressed: closeRequested()

    Image {
        id: preview
        objectName: "detailPreview"
        anchors {
            fill: parent
            margins: 48
            bottomMargin: 96
        }
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        source: detail.mediaKind === 1 && detail.absolutePath !== ""
                ? "file:" + detail.absolutePath
                : detail.thumbnailStatus === 2 ? detail.thumbnailSource : ""
    }

    BusyIndicator {
        anchors.centerIn: preview
        running: preview.status === Image.Loading
        visible: running
    }

    Label {
        anchors.centerIn: preview
        color: "#aaaaaa"
        visible: detail.mediaKind === 2 && preview.status !== Image.Ready
        text: detail.thumbnailStatus === 3
              ? qsTr("Video preview unavailable")
              : qsTr("Loading video preview…")
    }

    Label {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 24
        }
        color: "#ffffff"
        elide: Text.ElideMiddle
        horizontalAlignment: Text.AlignHCenter
        text: detail.captureTime === ""
              ? detail.absolutePath
              : detail.absolutePath + "\n" + detail.captureTime
    }

    ToolButton {
        objectName: "closeDetailButton"
        anchors {
            top: parent.top
            right: parent.right
            margins: 12
        }
        text: qsTr("Close")
        onClicked: detail.closeRequested()
    }
}
