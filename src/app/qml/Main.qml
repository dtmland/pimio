import QtQuick
import QtQuick.Window

Window {
    id: root

    objectName: "pimioMainWindow"
    width: 1024
    height: 720
    title: qsTr("pimio")

    Text {
        anchors.centerIn: parent
        objectName: "placeholderLabel"
        text: qsTr("pimio %1").arg(Qt.application.version)
    }
}
