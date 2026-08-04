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

    // When the full-resolution original is an image format Qt cannot decode
    // (for example a WebP or AVIF build without the matching image plugin),
    // fall back to the thumbnail the renderer already produced so the detail
    // view still shows the picture instead of an empty pane. Latched (rather
    // than derived from preview.status) so switching the source to the
    // thumbnail cannot flip this back and cause a binding loop.
    property bool fullResFailed: false

    // Reset the fallback latch whenever the item being shown changes.
    onAbsolutePathChanged: fullResFailed = false

    Image {
        id: preview
        objectName: "detailPreview"

        readonly property bool fullResRequested:
            detail.mediaKind === 1 && detail.absolutePath !== "" && !detail.fullResFailed

        anchors {
            fill: parent
            margins: 48
            bottomMargin: 96
        }
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        source: fullResRequested
                ? "file:" + (detail.absolutePath.startsWith("/") ? "" : "/")
                  + detail.absolutePath
                : detail.thumbnailStatus === 2 ? detail.thumbnailSource : ""
        onStatusChanged: {
            if (status === Image.Error && fullResRequested) {
                detail.fullResFailed = true;
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: preview
        running: preview.status === Image.Loading
        visible: running
    }

    Label {
        anchors.centerIn: preview
        color: "#aaaaaa"
        // Shown when neither the original nor the thumbnail could be displayed:
        // an unrenderable video, or an image whose full-resolution decode failed
        // and which has no usable thumbnail to fall back to.
        visible: preview.status !== Image.Ready && preview.status !== Image.Loading
                 && (detail.mediaKind === 2 || detail.fullResFailed)
        text: detail.thumbnailStatus === 3
              ? qsTr("Preview unavailable")
              : qsTr("Loading preview…")
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
