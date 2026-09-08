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
    property var session: null
    property string caption
    property int rating
    property string tags
    // Whether holding an arrow key jumps progressively further.
    property bool keyRepeatAcceleration: true
    signal closeRequested()
    // Asks the view that owns this preview to move by delta items in its own
    // order, so the preview always follows what the grid is showing.
    signal stepRequested(int delta)

    objectName: "detailView"
    color: "#ee1b1b1b"
    visible: false
    focus: visible

    // A held arrow key steps further per repeat, but more gently than in the
    // grid: each step here decodes another full-size image, so the ceiling is
    // low enough that the preview keeps up with the keyboard.
    readonly property int maximumKeyStep: 4
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
        return Math.min(maximumKeyStep, 1 + Math.floor(keyRepeatCount / 4))
    }

    function endKeyRepeat() {
        keyRepeatCount = 0
        lastNavigationKey = 0
    }

    onVisibleChanged: if (!visible) endKeyRepeat()

    Keys.onEscapePressed: closeRequested()

    Keys.onPressed: function(event) {
        const step = detail.navigationStep(event)
        if (event.key === Qt.Key_Left) {
            detail.stepRequested(-step)
        } else if (event.key === Qt.Key_Right) {
            detail.stepRequested(step)
        } else {
            return
        }
        event.accepted = true
    }

    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat)
            detail.endKeyRepeat()
    }

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

        Column {
            anchors {
                left: parent.left
                bottom: parent.bottom
                margins: 16
            }
            width: Math.min(420, parent.width - 32)
            spacing: 4

            TextField {
                id: captionField
                objectName: "editCaption"
                width: parent.width
                placeholderText: qsTr("Caption")
                text: detail.caption
            }
            TextField {
                id: tagsField
                objectName: "editTags"
                width: parent.width
                placeholderText: qsTr("Tags (comma separated)")
                text: detail.tags
            }
            SpinBox {
                id: ratingField
                objectName: "editRating"
                from: 0
                to: 5
                value: detail.rating
            }
            Row {
                spacing: 4
                SpinBox { id: cropX; from: 0; to: 100000; value: 0 }
                SpinBox { id: cropY; from: 0; to: 100000; value: 0 }
                SpinBox { id: cropWidth; from: 1; to: 100000; value: 1 }
                SpinBox { id: cropHeight; from: 1; to: 100000; value: 1 }
                ToolButton {
                    objectName: "cropButton"
                    text: qsTr("Crop")
                    enabled: detail.session && detail.mediaKind === 1
                    onClicked: detail.session.cropImage(detail.mediaId, cropX.value, cropY.value,
                                                        cropWidth.value, cropHeight.value)
                }
            }
            Row {
                spacing: 4
                ToolButton {
                    objectName: "rotateLeftButton"
                    text: qsTr("Rotate left")
                    enabled: detail.session && detail.mediaKind === 1
                    onClicked: {
                        if (detail.session.rotateImage(detail.mediaId, -90))
                            preview.rotation = preview.rotation - 90
                    }
                    SpinBox {
                        id: orientationField
                        objectName: "orientationValue"
                        from: 1
                        to: 8
                        value: 1
                    }
                    ToolButton {
                        objectName: "orientationButton"
                        text: qsTr("Orientation")
                        enabled: detail.session && detail.mediaKind === 1
                        onClicked: detail.session.orientImage(detail.mediaId, orientationField.value)
                    }
                }
                ToolButton {
                    objectName: "rotateRightButton"
                    text: qsTr("Rotate right")
                    enabled: detail.session && detail.mediaKind === 1
                    onClicked: {
                        if (detail.session.rotateImage(detail.mediaId, 90))
                            preview.rotation = preview.rotation + 90
                    }
                }
                ToolButton {
                    objectName: "saveEditsButton"
                    text: qsTr("Save")
                    enabled: detail.session
                    onClicked: {
                        detail.session.stageMetadata(detail.mediaId, captionField.text,
                                                     ratingField.value, tagsField.text)
                        detail.session.saveEdits()
                    }
                }
                ToolButton {
                    objectName: "discardEditsButton"
                    text: qsTr("Discard")
                    enabled: detail.session && detail.session.hasStagedEdits
                    onClicked: detail.session.discardEdits()
                }
            }
            TextField {
                id: exportPath
                objectName: "exportPath"
                width: parent.width
                placeholderText: qsTr("Export path (.jpg, .png, …)")
            }
            ToolButton {
                objectName: "exportButton"
                text: qsTr("Export")
                enabled: detail.session && exportPath.text !== ""
                onClicked: {
                    detail.session.stageMetadata(detail.mediaId, captionField.text,
                                                 ratingField.value, tagsField.text)
                    detail.session.exportEdited(detail.mediaId, exportPath.text)
                }
            }
            Label {
                width: parent.width
                color: "#ffcc80"
                wrapMode: Text.Wrap
                visible: detail.session && detail.session.editStatus !== ""
                text: detail.session ? detail.session.editStatus : ""
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
