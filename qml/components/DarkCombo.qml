import QtQuick
import QtQuick.Window

Item {
    id: root
    property var model: []
    property string currentKey: ""
    property string currentLabel: ""
    property string currentIcon: ""
    property bool opened: false
    signal activated(string key)

    height: 44

    function itemIcon(item) {
        if (!item)
            return ""
        return item.icon || ""
    }

    function syncLabel() {
        for (let i = 0; i < model.length; ++i) {
            if (model[i].key === currentKey) {
                currentLabel = model[i].label
                currentIcon = itemIcon(model[i])
                return
            }
        }
        currentLabel = model.length ? model[0].label : ""
        currentIcon = model.length ? itemIcon(model[0]) : ""
    }

    function openMenu() {
        const win = Window.window
        if (!win)
            return

        const content = win.contentItem
        dismissLayer.parent = content
        dismissLayer.x = 0
        dismissLayer.y = 0
        dismissLayer.width = content.width
        dismissLayer.height = content.height
        dismissLayer.visible = true
        dismissLayer.z = 10000

        const p = root.mapToItem(content, 0, root.height + 4)
        dropdown.parent = content
        dropdown.x = p.x
        dropdown.y = p.y
        dropdown.width = root.width
        dropdown.visible = true
        dropdown.z = 10001

        opened = true
    }

    function closeMenu() {
        opened = false
        dropdown.visible = false
        dismissLayer.visible = false
        dropdown.parent = root
        dismissLayer.parent = root
    }

    function toggleMenu() {
        if (opened)
            closeMenu()
        else
            openMenu()
    }

    onModelChanged: syncLabel()
    onCurrentKeyChanged: syncLabel()
    Component.onCompleted: syncLabel()

    Connections {
        target: typeof settingsController !== "undefined" ? settingsController : null
        function onLanguageChanged() { root.syncLabel() }
        ignoreUnknownSignals: true
    }

    Rectangle {
        id: box
        anchors.fill: parent
        radius: 10
        color: "#32353c"
        border.width: 2
        border.color: root.opened ? "#9aa0aa" : "#7a808c"

        Image {
            id: currentIconImg
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            visible: root.currentIcon.length > 0
            source: root.currentIcon
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        Text {
            anchors.left: currentIconImg.visible ? currentIconImg.right : parent.left
            anchors.right: arrow.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: currentIconImg.visible ? 8 : 12
            anchors.rightMargin: 8
            text: root.currentLabel
            color: "#f2f2f2"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 14
            elide: Text.ElideRight
        }

        Text {
            id: arrow
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.opened ? "▴" : "▾"
            color: "#8a8a8a"
            font.pixelSize: 14
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleMenu()
        }
    }

    MouseArea {
        id: dismissLayer
        visible: false
        onClicked: root.closeMenu()
    }

    Rectangle {
        id: dropdown
        visible: false
        width: root.width
        height: Math.min(Math.max(root.model.length, 1) * 40 + 12, 280)
        radius: 10
        color: "#2a2c32"
        border.width: 1
        border.color: "#7a808c"
        clip: true

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1

            delegate: Rectangle {
                width: list.width
                height: 40
                radius: 6
                color: rowMa.containsMouse ? "#3a3d45" : "#2a2c32"

                readonly property string rowIcon: (modelData && modelData.icon) ? modelData.icon : ""

                Image {
                    id: rowIconImg
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    visible: parent.rowIcon.length > 0
                    source: parent.rowIcon
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                Text {
                    anchors.left: rowIconImg.visible ? rowIconImg.right : parent.left
                    anchors.leftMargin: rowIconImg.visible ? 8 : 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: "#f2f2f2"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 14
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.currentKey = modelData.key
                        root.activated(modelData.key)
                        root.closeMenu()
                    }
                }
            }
        }
    }
}
