import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CheetahClip

ApplicationWindow {
    id: root
    width: 940
    height: 720
    minimumWidth: 900
    minimumHeight: 680
    visible: true
    // 依赖 language，切换中英文时整页文案重算
    readonly property string lang: appI18n.language
    title: {
        void lang
        return appI18n.tr("settings_title")
    }
    color: "#111214"
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint | Qt.WindowCloseButtonHint

    function t(key) {
        void root.lang
        return appI18n.tr(key)
    }

    property string hotkeyHint: t("hotkey_hint")

    function clearFieldFocus() {
        if (hotkeyField.inputItem && hotkeyField.inputItem.activeFocus)
            hotkeyField.clearFocus()
        else if (root.activeFocusItem && root.activeFocusItem !== root.contentItem)
            root.activeFocusItem.focus = false
    }

    Connections {
        target: appI18n
        function onLanguageChanged() {
            // 提示语按当前语言重置为默认说明（若仍是状态文案则刷新）
            hotkeyHint = appI18n.tr("hotkey_hint")
        }
    }

    // 点击空白处：取消输入框焦点/全选（子控件自己的 MouseArea 会优先接收点击）
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.clearFieldFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        // Header
        Text {
            text: t("settings_heading")
            color: "#f2f2f2"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 28
            font.bold: true
        }

        // 01 | 02
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            RoundedCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 8

                    Text {
                        text: t("section_capture")
                        color: "#f2f2f2"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 16
                        font.bold: true
                        Layout.bottomMargin: 4
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: t("hotkey")
                            color: "#8a8a8a"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 12
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: t("reset_default")
                            color: "#8a8a8a"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 12
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                onEntered: parent.color = "#f2f2f2"
                                onExited: parent.color = "#8a8a8a"
                                onClicked: {
                                    settingsController.resetHotkey()
                                    hotkeyHint = t("hotkey_reset")
                                }
                            }
                        }
                    }

                    DarkField {
                        id: hotkeyField
                        Layout.fillWidth: true
                        text: settingsController.hotkeyLabel
                        readOnly: true
                        onFocused: hotkeyHint = t("hotkey_press")
                        onBlurred: hotkeyHint = t("hotkey_hint")
                        onKeyPressed: (event) => {
                            let mods = []
                            if (event.modifiers & Qt.ControlModifier) mods.push("ctrl")
                            if (event.modifiers & Qt.AltModifier) mods.push("alt")
                            if (event.modifiers & Qt.ShiftModifier) mods.push("shift")
                            if (event.modifiers & Qt.MetaModifier) mods.push("win")
                            let key = ""
                            if (event.text.length === 1 && /[a-zA-Z0-9]/.test(event.text))
                                key = event.text.toLowerCase()
                            else if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F24)
                                key = "f" + (event.key - Qt.Key_F1 + 1)
                            if (mods.length === 0 || key.length === 0) {
                                hotkeyHint = t("hotkey_need_modifier")
                                event.accepted = true
                                return
                            }
                            settingsController.recordHotkeyFromKeys(mods, key)
                            hotkeyHint = t("hotkey_recorded")
                            event.accepted = true
                        }
                    }
                    Text {
                        text: hotkeyHint
                        color: "#5c5c5c"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                    }

                    Text {
                        text: t("select_mode")
                        color: "#8a8a8a"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                        Layout.topMargin: 4
                    }
                    DarkCombo {
                        Layout.fillWidth: true
                        model: settingsController.modeOptions
                        currentKey: settingsController.selectMode
                        onActivated: (key) => settingsController.selectMode = key
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: t("fixed_width")
                                color: "#8a8a8a"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 12
                            }
                            DarkField {
                                Layout.fillWidth: true
                                text: String(settingsController.fixedWidth)
                                onTextChanged: {
                                    const n = parseInt(text)
                                    if (!isNaN(n)) settingsController.fixedWidth = n
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: t("fixed_height")
                                color: "#8a8a8a"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: 12
                            }
                            DarkField {
                                Layout.fillWidth: true
                                text: String(settingsController.fixedHeight)
                                onTextChanged: {
                                    const n = parseInt(text)
                                    if (!isNaN(n)) settingsController.fixedHeight = n
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            RoundedCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 8

                    Text {
                        text: t("section_ai")
                        color: "#f2f2f2"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 16
                        font.bold: true
                        Layout.bottomMargin: 4
                    }

                    Text {
                        text: t("site")
                        color: "#8a8a8a"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                    }
                    DarkCombo {
                        Layout.fillWidth: true
                        model: settingsController.siteOptions
                        currentKey: settingsController.targetSite
                        onActivated: (key) => settingsController.targetSite = key
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Text {
                            text: t("use_desktop")
                            color: "#f2f2f2"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 14
                            Layout.fillWidth: true
                        }
                        ToggleSwitch {
                            checked: settingsController.useDesktopClient
                            onToggled: (v) => settingsController.useDesktopClient = v
                        }
                    }
                    Row {
                        spacing: 6
                        Text {
                            text: "●"
                            color: settingsController.desktopHintColor
                            font.pixelSize: 12
                        }
                        Text {
                            width: 320
                            wrapMode: Text.WordWrap
                            text: settingsController.desktopHint
                            color: "#8a8a8a"
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        text: t("browser")
                        color: "#8a8a8a"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                        Layout.topMargin: 4
                    }
                    DarkCombo {
                        Layout.fillWidth: true
                        model: settingsController.browserOptions
                        currentKey: settingsController.browser
                        onActivated: (key) => settingsController.browser = key
                    }

                    Text {
                        text: t("prompt")
                        color: "#8a8a8a"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 12
                        Layout.topMargin: 4
                    }
                    DarkField {
                        Layout.fillWidth: true
                        text: settingsController.promptText
                        onTextChanged: settingsController.promptText = text
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        // 03：按设计图横排 — 标题 | 小语言框 | 中缝 | 开机启动
        RoundedCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 96

            Item {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.topMargin: 18
                anchors.bottomMargin: 18

                Text {
                    id: generalTitle
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: t("section_general")
                    color: "#f2f2f2"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 16
                    font.bold: true
                }

                // 语言下拉（去掉「界面语言」标签）
                DarkCombo {
                    id: langCombo
                    anchors.left: generalTitle.right
                    anchors.leftMargin: 32
                    anchors.verticalCenter: parent.verticalCenter
                    width: 240
                    height: 40
                    model: settingsController.languageOptions
                    currentKey: settingsController.language
                    onActivated: (key) => settingsController.language = key
                }

                // 中缝对齐上方 01|02
                Rectangle {
                    width: 1
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: "#4a4d56"
                }

                Item {
                    anchors.left: parent.horizontalCenter
                    anchors.leftMargin: 24
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 40

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: t("autostart")
                        color: "#f2f2f2"
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: 14
                    }
                    ToggleSwitch {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: settingsController.startWithWindows
                        onToggled: (v) => settingsController.startWithWindows = v
                    }
                }
            }
        }

        // Footer
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: t("settings_footer_hint")
                color: "#5c5c5c"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            RoundButton {
                text: t("cancel")
                onClicked: settingsController.cancel()
            }
            RoundButton {
                text: t("save")
                primary: true
                onClicked: settingsController.save()
            }
        }
    }
}
