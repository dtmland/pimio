import QtQuick

Rectangle {
    id: tile

    required property int modelIndex
    required property string mediaId
    required property int thumbnailStatus
    required property int mediaKind
    required property var mediaModel
    required property bool showTileDiagnostics

    signal activated(int index)

    readonly property int readyThumbnailStatus: 2 // ThumbnailStatus::Ready
    readonly property int errorThumbnailStatus: 3 // ThumbnailStatus::Error
    readonly property int videoMediaKind: 2 // MediaKind::Video

    color: "#3c3c3c"
    radius: 4

    TapHandler {
        onTapped: tile.activated(tile.modelIndex)
    }

    // Thumbnail or placeholder
    Image {
        objectName: "gridThumbnail"
        anchors.fill: parent
        anchors.margins: 2
        cache: false
        fillMode: Image.PreserveAspectCrop
        visible: tile.thumbnailStatus === tile.readyThumbnailStatus
        source: tile.thumbnailStatus === tile.readyThumbnailStatus
                ? "image://thumbnail/" + tile.mediaId
                : ""

        // The model says this row has a thumbnail but the provider
        // could not serve it. Ask the model to render it again rather
        // than leaving a grey tile that nothing would ever fix. Once
        // per delegate: a file that genuinely cannot be rendered must
        // not turn into an endless re-request loop.
        property bool retried: false
        onSourceChanged: retried = false
        onStatusChanged: {
            if (status === Image.Error && !retried && tile.mediaModel
                    && typeof tile.mediaModel.refreshThumbnail === "function") {
                retried = true
                tile.mediaModel.refreshThumbnail(tile.modelIndex)
            }
        }
    }

    // Placeholder while loading
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        color: "transparent"
        visible: tile.thumbnailStatus !== tile.readyThumbnailStatus
        Text {
            anchors.centerIn: parent
            color: "#888888"
            font.pixelSize: 11
            text: tile.thumbnailStatus === tile.errorThumbnailStatus ? qsTr("Error") : qsTr("…")
        }
    }

    // Video badge
    Rectangle {
        anchors { bottom: parent.bottom; right: parent.right; margins: 4 }
        width: 36; height: 18; radius: 3
        color: "#cc000000"
        visible: tile.mediaKind === tile.videoMediaKind
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
        visible: tile.showTileDiagnostics
        Text {
            id: diagnosticsLabel
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 10
            text: tile.modelIndex + " · " + tile.thumbnailStatus
        }
    }
}
