import QtQuick

// A hand-drawn camera icon for the empty-library placeholder.
// Separated from Main.qml to keep the main window file within the target
// file-size budget.
Item {
    width: 72
    height: 60

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
