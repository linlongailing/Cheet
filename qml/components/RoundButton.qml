import QtQuick

Rectangle {
    id: root
    property string text: ""
    property bool primary: false
    signal clicked()

    width: Math.max(96, label.implicitWidth + 36)
    height: 36
    radius: 10
    color: primary ? (area.containsMouse ? "#f0f0f0" : "#ffffff")
                   : (area.containsMouse ? "#242424" : "#1a1b1e")
    border.width: primary ? 0 : 1
    border.color: area.containsMouse ? "#444444" : "#4a4d56"

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: primary ? "#111111" : "#f2f2f2"
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 14
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
