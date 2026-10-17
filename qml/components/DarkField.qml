import QtQuick

Rectangle {
    id: root
    property alias text: input.text
    property alias inputItem: input
    property bool readOnly: false
    property string placeholder: ""
    signal accepted()
    signal focused()
    signal blurred()
    signal keyPressed(var event)

    height: 44
    radius: 10
    color: "#32353c"
    border.width: 2
    border.color: input.activeFocus ? "#9aa0aa" : "#7a808c"

    function forceActiveFocus() { input.forceActiveFocus() }
    function clearFocus() {
        input.deselect()
        input.focus = false
    }

    // 仅设置光标，不拦截鼠标，以便 TextInput 可选中/拖选/删除
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        verticalAlignment: Text.AlignVCenter
        color: "#f2f2f2"
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 14
        readOnly: root.readOnly
        selectByMouse: true
        mouseSelectionMode: TextInput.SelectCharacters
        persistentSelection: false
        clip: true
        onActiveFocusChanged: {
            if (activeFocus) {
                root.focused()
            } else {
                deselect()
                root.blurred()
            }
        }
        Keys.onReturnPressed: root.accepted()
        Keys.onPressed: (event) => root.keyPressed(event)

        // 双击全选（覆盖默认的选词）
        TapHandler {
            exclusiveSignals: TapHandler.DoubleTap
            onDoubleTapped: {
                input.forceActiveFocus()
                input.selectAll()
            }
        }
    }
}
