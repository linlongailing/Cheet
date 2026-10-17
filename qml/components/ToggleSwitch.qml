import QtQuick

Item {
    id: root
    width: 44
    height: 24
    property bool checked: false
    signal toggled(bool value)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? "#c8c8c8" : "#2a2a2a"

        Rectangle {
            width: 18
            height: 18
            radius: 9
            anchors.verticalCenter: parent.verticalCenter
            x: root.checked ? parent.width - width - 3 : 3
            color: root.checked ? "#ffffff" : "#8a8a8a"

            Behavior on x { NumberAnimation { duration: 120 } }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
}
