import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

ApplicationWindow {
    id: win
    width: 1200
    height: 800
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: "Slate"
    color: theme.background

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground

    readonly property bool rightToLeft: Qt.application.layoutDirection === Qt.RightToLeft
    // Bare keys belong to whatever is being typed into.
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool keys: !typing && !help.visible && !confirm.visible
    readonly property color panel: Qt.tint(theme.background, Qt.alpha(theme.foreground, 0.05))
    readonly property var swatches: ["", "red", "orange", "yellow", "green", "cyan", "blue", "magenta"]

    // The shapes share one toolbar button, which shows the one last drawn.
    readonly property var shapes: [
        { tool: "rect", key: "R", label: qsTr("Rectangle"), icon: "M3 4.5 H17 V15.5 H3 Z" },
        { tool: "ellipse", key: "O", label: qsTr("Circle"), icon: "M10 3 A7 7 0 1 0 10 17 A7 7 0 1 0 10 3 Z" },
        { tool: "triangle", key: "T", label: qsTr("Triangle"), icon: "M10 3 L17.5 16.5 H2.5 Z" },
        { tool: "rhombus", key: "D", label: qsTr("Rhombus"), icon: "M10 2.5 L17.5 10 L10 17.5 L2.5 10 Z" },
        { tool: "arrow", key: "W", label: qsTr("Arrow"), icon: "M2.5 7.5 H11 V4 L17.5 10 L11 16 V12.5 H2.5 Z" },
        { tool: "line", key: "L", label: qsTr("Line"), icon: "M3 17 L17 3" },
        { tool: "frame", key: "F", label: qsTr("Frame"), icon: "M6 2 V18 M14 2 V18 M2 6 H18 M2 14 H18" }
    ]
    // What each icon is called, for the menu's tooltips.
    readonly property var iconLabels: ({
        star: qsTr("Star"), heart: qsTr("Heart"), check: qsTr("Check"), cross: qsTr("Cross"),
        warning: qsTr("Warning"), info: qsTr("Information"), question: qsTr("Question"), idea: qsTr("Idea"),
        flag: qsTr("Flag"), person: qsTr("Person"), clock: qsTr("Clock"), calendar: qsTr("Calendar"),
        lock: qsTr("Lock"), bolt: qsTr("Lightning"), target: qsTr("Target"), home: qsTr("Home"),
        mail: qsTr("Mail"), chat: qsTr("Chat"), search: qsTr("Search"), location: qsTr("Location"),
        arrow: qsTr("Arrow")
    })

    property string lastShape: "rect"
    readonly property var shownShape: shapes.find(shape => shape.tool === lastShape)
    readonly property bool drawingShape: shapes.some(shape => shape.tool === view.tool)

    Connections {
        target: view
        function onToolChanged() {
            if (win.drawingShape)
                win.lastShape = view.tool
        }
    }

    property string renamingId: ""

    function nudge(dx, dy) {
        if (board.selectionCount === 0)
            return
        board.checkpoint()
        board.moveSelection(dx, dy)
    }

    function paint(index) { board.setSelectionColor(swatches[index]) }

    // Where a connection's arrow can be. A steps through them, and the
    // connect button shows the one the next connection takes.
    readonly property var connections: [
        { arrow: "", label: qsTr("Arrow at the end"),
          icon: "M3 10 H16.5 M12 5.5 L16.5 10 L12 14.5" },
        { arrow: "start", label: qsTr("Arrow at the start"),
          icon: "M3.5 10 H17 M8 5.5 L3.5 10 L8 14.5" },
        { arrow: "none", label: qsTr("No arrow"),
          icon: "M3 10 H17" }
    ]
    readonly property var shownConnection: connections.find(c => c.arrow === board.shownArrow) || connections[0]
    function nextArrow() {
        const at = connections.findIndex(c => c.arrow === board.shownArrow)
        board.setSelectionArrow(connections[(at + 1) % connections.length].arrow)
    }

    // How an outline can be drawn. S steps through them.
    readonly property var strokes: [
        { style: "", label: qsTr("Solid") },
        { style: "dashed", label: qsTr("Dashed") },
        { style: "dotted", label: qsTr("Dotted") }
    ]
    function nextStroke() {
        const at = strokes.findIndex(s => s.style === board.shownStroke)
        board.setSelectionStroke(strokes[(at + 1) % strokes.length].style)
    }

    // A short line drawn the way an outline of that style is.
    component StrokeSample: Shape {
        id: sample
        property string style: ""
        property real thickness: 2
        height: thickness
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: theme.foreground
            strokeWidth: sample.thickness
            fillColor: "transparent"
            strokeStyle: sample.style ? ShapePath.DashLine : ShapePath.SolidLine
            dashPattern: sample.style === "dashed" ? [4, 3] : [0.1, 2.5]
            capStyle: sample.style === "dashed" ? ShapePath.FlatCap : ShapePath.RoundCap
            startX: sample.thickness / 2
            startY: sample.thickness / 2
            PathLine { x: sample.width - sample.thickness / 2; y: sample.thickness / 2 }
        }
    }

    // Saves the board in Downloads, drawn as it is here, and says where.
    property string notice: ""
    property bool noticeFailed: false
    function say(text, failed) {
        noticeFailed = failed
        notice = text
        noticeTimer.restart()
    }
    function download(format) {
        const result = backend.exportBoard(format, theme.colors, theme.background, theme.foreground,
                                           theme.light, theme.radius)
        say(result.ok ? qsTr("Saved to %1").arg(result.path) : qsTr("Could not save to %1").arg(result.path),
            !result.ok)
    }

    // Puts image files on the board: where they were dropped, or in the
    // middle of what is shown when they were picked in the file dialog,
    // since the pointer was last on the dialog. Each is as large as it is,
    // unless that is more than most of the window, and several are fanned
    // out so none hides another.
    function addImages(urls, at) {
        const spot = view.toWorld(at || Qt.point(view.width / 2, view.height / 2))
        const added = []
        let unread = ""
        for (const url of urls) {
            const image = backend.importImage(url)
            if (!image.ok) {
                unread = image.file
                continue
            }
            const fit = Math.min(1, view.width * 0.6 / view.zoom / image.width,
                                 view.height * 0.6 / view.zoom / image.height)
            const w = image.width * fit, h = image.height * fit, step = added.length * 24
            added.push(board.addImage(image.name, spot.x - w / 2 + step, spot.y - h / 2 + step, w, h))
        }
        if (added.length > 0) {
            view.tool = "select"
            board.selectOnly(added[0])
            for (const id of added.slice(1))
                board.toggle(id)
        }
        if (unread)
            say(qsTr("Could not read %1 as an image").arg(unread), true)
    }
    Connections {
        target: backend
        function onImagesChosen(urls) { win.addImages(urls) }
        function onPickFailed() { win.say(qsTr("Could not open the file dialog"), true) }
    }
    Timer {
        id: noticeTimer
        interval: 5000
        onTriggered: win.notice = ""
    }

    function askToDelete(id, name) {
        confirm.boardId = id
        confirm.boardName = name
        confirm.open()
    }

    Shortcut { sequence: "?"; enabled: !win.typing && !confirm.visible; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequence: "Q"; enabled: win.keys; onActivated: Qt.quit() }

    Shortcut { sequence: "V"; enabled: win.keys; onActivated: view.tool = "select" }
    Shortcut { sequence: "R"; enabled: win.keys; onActivated: view.tool = "rect" }
    Shortcut { sequence: "O"; enabled: win.keys; onActivated: view.tool = "ellipse" }
    // T is the triangle, and pressed again, text.
    Shortcut { sequence: "T"; enabled: win.keys; onActivated: view.tool = view.tool === "triangle" ? "text" : "triangle" }
    Shortcut { sequence: "D"; enabled: win.keys; onActivated: view.tool = "rhombus" }
    Shortcut { sequence: "W"; enabled: win.keys; onActivated: view.tool = "arrow" }
    Shortcut { sequence: "X"; enabled: win.keys; onActivated: view.tool = "text" }
    Shortcut { sequence: "L"; enabled: win.keys; onActivated: view.tool = "line" }
    Shortcut { sequence: "F"; enabled: win.keys; onActivated: view.tool = "frame" }
    Shortcut { sequence: "P"; enabled: win.keys; onActivated: view.tool = "postit" }
    Shortcut { sequence: "M"; enabled: win.keys; onActivated: backend.pickImages() }
    Shortcut { sequence: "I"; enabled: win.keys; onActivated: iconMenu.visible ? iconMenu.close() : iconMenu.open() }
    Shortcut { sequence: ","; enabled: win.keys; onActivated: board.rotateSelection(-15) }
    Shortcut { sequence: "."; enabled: win.keys; onActivated: board.rotateSelection(15) }
    Shortcut { sequence: "C"; enabled: win.keys; onActivated: view.tool = "connect" }
    Shortcut { sequence: "S"; enabled: win.keys; onActivated: win.nextStroke() }
    Shortcut { sequence: "A"; enabled: win.keys; onActivated: win.nextArrow() }
    Shortcut {
        sequence: "Escape"
        enabled: win.keys && !colorMenu.visible && !strokeMenu.visible && !connectMenu.visible && !arrangeMenu.visible && !shapeMenu.visible && !boardMenu.visible && !iconMenu.visible
        onActivated: {
            if (view.tool !== "select")
                view.tool = "select"
            else
                board.clearSelection()
        }
    }

    Shortcut { sequences: ["Return", "Enter"]; enabled: win.keys; onActivated: if (board.selectedId) view.beginEdit(board.selectedId) }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: win.keys; onActivated: board.removeSelection() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: win.keys; onActivated: board.selectAll() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: win.keys; onActivated: board.copy() }
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: win.keys
        onActivated: {
            const p = view.insertPoint()
            board.paste(p.x, p.y)
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: win.keys
        onActivated: {
            const p = view.insertPoint()
            board.duplicate(p.x, p.y, view.pointerInside)
        }
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: win.keys; onActivated: board.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: win.keys; onActivated: board.redo() }

    Shortcut { sequence: "Left"; enabled: win.keys; onActivated: win.nudge(-1, 0) }
    Shortcut { sequence: "Right"; enabled: win.keys; onActivated: win.nudge(1, 0) }
    Shortcut { sequence: "Up"; enabled: win.keys; onActivated: win.nudge(0, -1) }
    Shortcut { sequence: "Down"; enabled: win.keys; onActivated: win.nudge(0, 1) }
    Shortcut { sequence: "Alt+Left"; enabled: win.keys; onActivated: board.alignSelection("left") }
    Shortcut { sequence: "Alt+Right"; enabled: win.keys; onActivated: board.alignSelection("right") }
    Shortcut { sequence: "Alt+Up"; enabled: win.keys; onActivated: board.alignSelection("top") }
    Shortcut { sequence: "Alt+Down"; enabled: win.keys; onActivated: board.alignSelection("bottom") }
    Shortcut { sequence: "Alt+H"; enabled: win.keys; onActivated: board.alignSelection("center") }
    Shortcut { sequence: "Alt+V"; enabled: win.keys; onActivated: board.alignSelection("middle") }
    Shortcut { sequence: "Alt+Shift+H"; enabled: win.keys; onActivated: board.alignSelection("spreadAcross") }
    Shortcut { sequence: "Alt+Shift+V"; enabled: win.keys; onActivated: board.alignSelection("spreadDown") }
    Shortcut { sequence: "Ctrl+G"; enabled: win.keys; onActivated: board.groupSelection() }
    Shortcut { sequence: "Ctrl+Shift+G"; enabled: win.keys; onActivated: board.ungroupSelection() }
    Shortcut { sequence: "]"; enabled: win.keys; onActivated: board.bringToFront() }
    Shortcut { sequence: "["; enabled: win.keys; onActivated: board.sendToBack() }
    Shortcut { sequence: "Shift+Left"; enabled: win.keys; onActivated: win.nudge(-10, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: win.keys; onActivated: win.nudge(10, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: win.keys; onActivated: win.nudge(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: win.keys; onActivated: win.nudge(0, 10) }

    Shortcut { sequences: ["+", "="]; enabled: win.keys; onActivated: view.zoomAt(view.focusPoint(), 1.25, true) }
    Shortcut { sequence: "-"; enabled: win.keys; onActivated: view.zoomAt(view.focusPoint(), 0.8, true) }
    Shortcut { sequence: "0"; enabled: win.keys; onActivated: view.resetZoom() }
    Shortcut { sequence: "Shift+F"; enabled: win.keys; onActivated: view.fit() }

    Shortcut { sequence: "1"; enabled: win.keys; onActivated: win.paint(0) }
    Shortcut { sequence: "2"; enabled: win.keys; onActivated: win.paint(1) }
    Shortcut { sequence: "3"; enabled: win.keys; onActivated: win.paint(2) }
    Shortcut { sequence: "4"; enabled: win.keys; onActivated: win.paint(3) }
    Shortcut { sequence: "5"; enabled: win.keys; onActivated: win.paint(4) }
    Shortcut { sequence: "6"; enabled: win.keys; onActivated: win.paint(5) }
    Shortcut { sequence: "7"; enabled: win.keys; onActivated: win.paint(6) }
    Shortcut { sequence: "8"; enabled: win.keys; onActivated: win.paint(7) }

    Shortcut { sequence: "B"; enabled: win.keys; onActivated: boardMenu.visible ? boardMenu.close() : boardMenu.open() }
    Shortcut { sequence: "Ctrl+N"; enabled: win.keys; onActivated: backend.newBoard() }
    Shortcut { sequence: "Ctrl+Tab"; enabled: win.keys; onActivated: backend.next() }
    Shortcut { sequence: "Ctrl+Shift+Tab"; enabled: win.keys; onActivated: backend.previous() }
    Shortcut {
        sequence: "F2"
        enabled: win.keys
        onActivated: {
            boardMenu.open()
            win.renamingId = backend.currentId
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction. The canvas itself is placed by
    // coordinates and stays as it was drawn.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: win.rightToLeft
        LayoutMirroring.childrenInherit: true

        CanvasView {
            id: view
            objectName: "view"
            anchors.fill: parent
        }

        // Image files dragged in from the file manager land where they
        // are let go.
        DropArea {
            anchors.fill: view
            keys: ["text/uri-list"]
            onDropped: (drop) => {
                win.addImages(drop.urls, Qt.point(drop.x, drop.y))
                drop.accept()
            }
        }

        Label {
            objectName: "hint"
            visible: board.count === 0
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 520)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: theme.colors.muted
            font.pixelSize: 16
            text: qsTr("Press R, O, T or L and drag to draw. Double-click to write. Press ? for every shortcut.")
        }

        // A save that failed is said, not hidden.
        Rectangle {
            visible: backend.saveError !== ""
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 16
            width: Math.min(parent.width - 32, errorLabel.implicitWidth + 32)
            height: errorLabel.implicitHeight + 16
            radius: theme.radius
            color: theme.colors.red
            Label {
                id: errorLabel
                anchors.centerIn: parent
                width: parent.width - 32
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: "black"
                text: qsTr("Could not save this board to %1").arg(backend.saveError)
            }
        }

        // The tools, and the colors.
        Rectangle {
            id: toolbar
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 20
            width: tools.implicitWidth + 16
            height: tools.implicitHeight + 16
            // A window tiled narrow still shows every tool.
            scale: Math.min(1, (parent.width - 24) / width)
            transformOrigin: Item.Bottom
            radius: theme.radius
            color: win.panel
            border.color: theme.colors.selection

            // Clicks on the bar don't fall through to the canvas.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (wheel) => wheel.accepted = true }

            RowLayout {
                id: tools
                anchors.centerIn: parent
                spacing: 4

                Repeater {
                    model: [
                        { tool: "select", key: "V", tip: qsTr("Select and move"),
                          icon: "M4 2 L4 16 L8 12.5 L10.5 18 L12.5 17 L10 11.5 L15 11.5 Z" },
                        { tool: "shapes", menu: "shapes", tip: qsTr("Shapes") },
                        { tool: "icons", menu: "icons", tip: qsTr("Icons") },
                        { tool: "postit", key: "P", tip: qsTr("Post-it"),
                          icon: "M3 3 H17 V12 L12 17 H3 Z M17 12 H12 V17" },
                        { tool: "text", key: "X", tip: qsTr("Text"),
                          icon: "M4 5.5 V3.5 H16 V5.5 M10 3.5 V16.5 M7.5 16.5 H12.5" },
                        { tool: "image", action: true, key: "M", tip: qsTr("Add images"),
                          icon: "M3 4 H17 V16 H3 Z M3 13.5 L7.5 9.5 L11 12.5 L13.5 10.5 L17 13.5 M12.6 7.5 A0.6 0.6 0 1 0 13.8 7.5 A0.6 0.6 0 1 0 12.6 7.5" },
                        { tool: "connect", menu: "connect", key: "C", tip: qsTr("Connect two shapes") }
                    ]
                    delegate: Rectangle {
                        id: toolButton
                        required property var modelData
                        // One entry stands for all the shapes, one for all the
                        // icons, and one for every way to connect; each opens
                        // its menu and shows the one last used.
                        readonly property string menu: modelData.menu || ""
                        readonly property Popup popup: menu === "shapes" ? shapeMenu : menu === "icons" ? iconMenu
                                                     : menu === "connect" ? connectMenu : null
                        readonly property bool active: menu === "shapes" ? win.drawingShape
                                                     : menu === "icons" ? view.tool === "icon"
                                                     : view.tool === modelData.tool
                        readonly property string icon: menu === "shapes" ? win.shownShape.icon
                                                     : menu === "icons" ? board.iconPath(view.iconName)
                                                     : menu === "connect" ? win.shownConnection.icon
                                                     : modelData.icon
                        // Icons are drawn on a 24 square, the toolbar's own on 20.
                        readonly property real iconBox: menu === "icons" ? 24 : 20
                        objectName: "tool-" + modelData.tool
                        Component.onCompleted: if (popup) popup.parent = toolButton
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        radius: Math.min(4, theme.radius)
                        color: active ? theme.accent
                             : toolHover.hovered || (popup && popup.visible) ? theme.colors.selection : "transparent"

                        Shape {
                            visible: toolButton.popup !== null
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 4
                            width: 8
                            height: 6
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: toolButton.active ? theme.accentForeground : theme.foreground
                                strokeWidth: 1.4
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                PathSvg { path: "M1 4.5 L4 1.5 L7 4.5" }
                            }
                        }
                        Shape {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: toolButton.popup ? -4 : 0
                            width: toolButton.iconBox
                            height: toolButton.iconBox
                            scale: 20 / toolButton.iconBox
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: toolButton.active ? theme.accentForeground : theme.foreground
                                strokeWidth: 1.6 * toolButton.iconBox / 20
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                PathSvg { path: toolButton.icon }
                            }
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: 4
                            anchors.bottomMargin: 1
                            text: toolButton.menu === "shapes" ? win.shownShape.key
                                : toolButton.menu === "icons" ? "I" : toolButton.modelData.key
                            font.pixelSize: 9
                            opacity: 0.6
                            color: toolButton.active ? theme.accentForeground : theme.foreground
                        }
                        HoverHandler { id: toolHover }
                        TapHandler {
                            onTapped: {
                                // The image button isn't a tool to draw
                                // with: it asks for files straight away.
                                if (toolButton.modelData.action)
                                    backend.pickImages()
                                else if (!toolButton.popup)
                                    view.tool = toolButton.modelData.tool
                                else if (toolButton.popup.visible)
                                    toolButton.popup.close()
                                else
                                    toolButton.popup.open()
                            }
                        }
                        ToolTip.visible: toolHover.hovered && !(popup && popup.visible)
                        ToolTip.delay: 500
                        ToolTip.text: modelData.tip
                    }
                }

                // The shapes' menu; its button above takes it as its own.
                Popup {
                    id: shapeMenu
                    objectName: "shapeMenu"
                    x: parent ? (parent.width - width) / 2 : 0
                    y: -height - 14
                    padding: 8
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        color: win.panel
                        border.color: theme.colors.selection
                        radius: theme.radius
                    }

                    ColumnLayout {
                        spacing: 2
                        // A popup is not an Item and doesn't inherit the
                        // window's mirroring, so its content opts in.
                        LayoutMirroring.enabled: win.rightToLeft
                        LayoutMirroring.childrenInherit: true

                        Repeater {
                            model: win.shapes
                            delegate: Rectangle {
                                id: shapeRow
                                required property var modelData
                                readonly property bool current: view.tool === modelData.tool
                                objectName: "shape-" + modelData.tool
                                Layout.fillWidth: true
                                Layout.preferredWidth: shapeLine.implicitWidth + 20
                                Layout.preferredHeight: 32
                                radius: Math.min(4, theme.radius)
                                color: current || shapeRowHover.hovered ? theme.colors.selection : "transparent"

                                RowLayout {
                                    id: shapeLine
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10

                                    Shape {
                                        Layout.preferredWidth: 20
                                        Layout.preferredHeight: 20
                                        preferredRendererType: Shape.CurveRenderer
                                        ShapePath {
                                            strokeColor: theme.foreground
                                            strokeWidth: 1.5
                                            fillColor: "transparent"
                                            capStyle: ShapePath.RoundCap
                                            joinStyle: ShapePath.RoundJoin
                                            PathSvg { path: shapeRow.modelData.icon }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: shapeRow.modelData.label
                                    }
                                    Label {
                                        Layout.leftMargin: 14
                                        text: shapeRow.modelData.key
                                        opacity: 0.6
                                    }
                                }
                                HoverHandler { id: shapeRowHover }
                                TapHandler {
                                    onTapped: {
                                        view.tool = shapeRow.modelData.tool
                                        shapeMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }

                // The ways to connect; picking one changes the selected
                // connections, or else takes up the connect tool with it.
                Popup {
                    id: connectMenu
                    objectName: "connectMenu"
                    x: parent ? (parent.width - width) / 2 : 0
                    y: -height - 14
                    padding: 8
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        color: win.panel
                        border.color: theme.colors.selection
                        radius: theme.radius
                    }

                    ColumnLayout {
                        spacing: 2
                        // A popup is not an Item and doesn't inherit the
                        // window's mirroring, so its content opts in.
                        LayoutMirroring.enabled: win.rightToLeft
                        LayoutMirroring.childrenInherit: true

                        Repeater {
                            model: win.connections
                            delegate: Rectangle {
                                id: connectRow
                                required property var modelData
                                readonly property bool current: board.shownArrow === modelData.arrow
                                objectName: "connect-" + (modelData.arrow || "end")
                                Layout.fillWidth: true
                                Layout.preferredWidth: connectLine.implicitWidth + 20
                                Layout.preferredHeight: 32
                                radius: Math.min(4, theme.radius)
                                color: current || connectRowHover.hovered ? theme.colors.selection : "transparent"

                                RowLayout {
                                    id: connectLine
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10

                                    Shape {
                                        Layout.preferredWidth: 20
                                        Layout.preferredHeight: 20
                                        preferredRendererType: Shape.CurveRenderer
                                        ShapePath {
                                            strokeColor: theme.foreground
                                            strokeWidth: 1.5
                                            fillColor: "transparent"
                                            capStyle: ShapePath.RoundCap
                                            joinStyle: ShapePath.RoundJoin
                                            PathSvg { path: connectRow.modelData.icon }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: connectRow.modelData.label
                                    }
                                }
                                HoverHandler { id: connectRowHover }
                                TapHandler {
                                    onTapped: {
                                        const restyles = board.connectorSelected
                                        board.setSelectionArrow(connectRow.modelData.arrow)
                                        if (!restyles)
                                            view.tool = "connect"
                                        connectMenu.close()
                                    }
                                }
                            }
                        }
                    }
                }

                // The icons' menu; like the shapes', its button takes it.
                Popup {
                    id: iconMenu
                    objectName: "iconMenu"
                    x: parent ? (parent.width - width) / 2 : 0
                    y: -height - 14
                    padding: 8
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        color: win.panel
                        border.color: theme.colors.selection
                        radius: theme.radius
                    }

                    GridLayout {
                        columns: 7
                        rowSpacing: 2
                        columnSpacing: 2
                        // A popup is not an Item and doesn't inherit the
                        // window's mirroring, so its content opts in.
                        LayoutMirroring.enabled: win.rightToLeft
                        LayoutMirroring.childrenInherit: true

                        Repeater {
                            model: board.iconNames
                            delegate: Rectangle {
                                id: iconCell
                                required property string modelData
                                readonly property bool current: view.tool === "icon" && view.iconName === modelData
                                objectName: "icon-" + modelData
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 38
                                radius: Math.min(4, theme.radius)
                                color: current || cellHover.hovered ? theme.colors.selection : "transparent"

                                Shape {
                                    anchors.centerIn: parent
                                    width: 24
                                    height: 24
                                    preferredRendererType: Shape.CurveRenderer
                                    ShapePath {
                                        strokeColor: theme.foreground
                                        strokeWidth: 1.6
                                        fillColor: "transparent"
                                        capStyle: ShapePath.RoundCap
                                        joinStyle: ShapePath.RoundJoin
                                        PathSvg { path: board.iconPath(iconCell.modelData) }
                                    }
                                }
                                HoverHandler { id: cellHover }
                                TapHandler {
                                    onTapped: {
                                        view.iconName = iconCell.modelData
                                        view.tool = "icon"
                                        iconMenu.close()
                                    }
                                }
                                ToolTip.visible: cellHover.hovered
                                ToolTip.delay: 400
                                ToolTip.text: win.iconLabels[modelData] || modelData
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 26
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    color: theme.colors.selection
                }

                // Every color behind one button; the number keys still pick them.
                Rectangle {
                    id: colorButton
                    objectName: "colorButton"
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: Math.min(4, theme.radius)
                    color: colorMenu.visible || colorHover.hovered ? theme.colors.selection : "transparent"

                    // A true circle, whatever the theme's corners.
                    Rectangle {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: -4
                        width: 20
                        height: 20
                        radius: 10
                        // What is selected, or what new shapes will take;
                        // a selection of several colors shows as a dash.
                        readonly property string shown: board.shownColor
                        readonly property bool mixed: shown === "mixed"
                        color: shown && !mixed ? theme.colors[shown] : "transparent"
                        border.color: shown && !mixed ? "transparent" : theme.foreground
                        Rectangle {
                            visible: parent.mixed
                            anchors.centerIn: parent
                            width: 8
                            height: 2
                            color: theme.foreground
                        }
                        border.width: 2
                    }
                    Shape {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 4
                        width: 8
                        height: 6
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: theme.foreground
                            strokeWidth: 1.4
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: "M1 4.5 L4 1.5 L7 4.5" }
                        }
                    }
                    HoverHandler { id: colorHover }
                    TapHandler { onTapped: colorMenu.visible ? colorMenu.close() : colorMenu.open() }
                    ToolTip.visible: colorHover.hovered && !colorMenu.visible
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Color")

                    Popup {
                        id: colorMenu
                        objectName: "colorMenu"
                        x: (parent.width - width) / 2
                        y: -height - 14
                        padding: 8
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        background: Rectangle {
                            color: win.panel
                            border.color: theme.colors.selection
                            radius: theme.radius
                        }

                        ColumnLayout {
                            spacing: 2
                            // A popup is not an Item and doesn't inherit the
                            // window's mirroring, so its content opts in.
                            LayoutMirroring.enabled: win.rightToLeft
                            LayoutMirroring.childrenInherit: true

                            Repeater {
                                model: win.swatches
                                delegate: Rectangle {
                                    id: swatch
                                    required property string modelData
                                    required property int index
                                    readonly property bool current: board.shownColor === modelData
                                    objectName: "color-" + (modelData || "none")
                                    Layout.preferredWidth: 76
                                    Layout.preferredHeight: 32
                                    radius: Math.min(4, theme.radius)
                                    color: current || swatchHover.hovered ? theme.colors.selection : "transparent"

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 10
                                        width: 18
                                        height: 18
                                        radius: 9
                                        color: swatch.modelData ? theme.colors[swatch.modelData] : "transparent"
                                        border.color: swatch.modelData ? "transparent" : theme.foreground
                                        border.width: 2
                                    }
                                    Label {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: 12
                                        text: swatch.index + 1
                                        opacity: 0.7
                                    }
                                    HoverHandler { id: swatchHover }
                                    TapHandler {
                                        onTapped: {
                                            win.paint(swatch.index)
                                            colorMenu.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Whether an outline is solid, dashed, or dotted.
                Rectangle {
                    id: strokeButton
                    objectName: "strokeButton"
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: Math.min(4, theme.radius)
                    color: strokeMenu.visible || strokeHover.hovered ? theme.colors.selection : "transparent"

                    // The style of what is selected, or of what is drawn
                    // next; a selection of several styles shows them all.
                    Column {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: -4
                        spacing: 3
                        readonly property bool mixed: board.shownStroke === "mixed"
                        Repeater {
                            model: parent.mixed ? ["", "dashed", "dotted"] : [board.shownStroke]
                            StrokeSample {
                                required property string modelData
                                width: 20
                                style: modelData
                            }
                        }
                    }
                    Shape {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 4
                        width: 8
                        height: 6
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: theme.foreground
                            strokeWidth: 1.4
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: "M1 4.5 L4 1.5 L7 4.5" }
                        }
                    }
                    HoverHandler { id: strokeHover }
                    TapHandler { onTapped: strokeMenu.visible ? strokeMenu.close() : strokeMenu.open() }
                    ToolTip.visible: strokeHover.hovered && !strokeMenu.visible
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Border style")

                    Popup {
                        id: strokeMenu
                        objectName: "strokeMenu"
                        x: (parent.width - width) / 2
                        y: -height - 14
                        padding: 8
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        background: Rectangle {
                            color: win.panel
                            border.color: theme.colors.selection
                            radius: theme.radius
                        }

                        ColumnLayout {
                            spacing: 2
                            // A popup is not an Item and doesn't inherit the
                            // window's mirroring, so its content opts in.
                            LayoutMirroring.enabled: win.rightToLeft
                            LayoutMirroring.childrenInherit: true

                            Repeater {
                                model: win.strokes
                                delegate: Rectangle {
                                    id: strokeRow
                                    required property var modelData
                                    readonly property bool current: board.shownStroke === modelData.style
                                    objectName: "stroke-" + (modelData.style || "solid")
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: strokeRowContent.implicitWidth + 22
                                    Layout.preferredHeight: 32
                                    radius: Math.min(4, theme.radius)
                                    color: current || strokeRowHover.hovered ? theme.colors.selection : "transparent"

                                    RowLayout {
                                        id: strokeRowContent
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 12
                                        spacing: 12
                                        StrokeSample {
                                            Layout.preferredWidth: 30
                                            Layout.alignment: Qt.AlignVCenter
                                            style: strokeRow.modelData.style
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: strokeRow.modelData.label
                                        }
                                    }
                                    HoverHandler { id: strokeRowHover }
                                    TapHandler {
                                        onTapped: {
                                            board.setSelectionStroke(strokeRow.modelData.style)
                                            strokeMenu.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Lining shapes up, and which of them is in front.
                Rectangle {
                    id: arrangeButton
                    objectName: "arrangeButton"
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: Math.min(4, theme.radius)
                    color: arrangeMenu.visible || arrangeHover.hovered ? theme.colors.selection : "transparent"

                    Shape {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: -4
                        width: 20
                        height: 20
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: theme.foreground
                            strokeWidth: 1.6
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: "M3 3 V17 M6 5.5 H16 V9 H6 Z M6 11 H11.5 V14.5 H6 Z" }
                        }
                    }
                    Shape {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 4
                        width: 8
                        height: 6
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: theme.foreground
                            strokeWidth: 1.4
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: "M1 4.5 L4 1.5 L7 4.5" }
                        }
                    }
                    HoverHandler { id: arrangeHover }
                    TapHandler { onTapped: arrangeMenu.visible ? arrangeMenu.close() : arrangeMenu.open() }
                    ToolTip.visible: arrangeHover.hovered && !arrangeMenu.visible
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Arrange")

                    Popup {
                        id: arrangeMenu
                        objectName: "arrangeMenu"
                        x: (parent.width - width) / 2
                        y: -height - 14
                        padding: 8
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        background: Rectangle {
                            color: win.panel
                            border.color: theme.colors.selection
                            radius: theme.radius
                        }

                        ColumnLayout {
                            spacing: 2
                            // A popup is not an Item and doesn't inherit the
                            // window's mirroring, so its content opts in.
                            LayoutMirroring.enabled: win.rightToLeft
                            LayoutMirroring.childrenInherit: true

                            Repeater {
                                // Aligning needs two things to line up,
                                // spacing needs a third between them, and
                                // the order needs one to move.
                                model: [
                                    { name: "left", label: qsTr("Align left"), key: "Alt+←", needs: 2,
                                      icon: "M3 3 V17 M6 5.5 H16 V9 H6 Z M6 11 H11.5 V14.5 H6 Z" },
                                    { name: "center", label: qsTr("Center horizontally"), key: "Alt+H", needs: 2,
                                      icon: "M10 3 V17 M4.5 5.5 H15.5 V9 H4.5 Z M7 11 H13 V14.5 H7 Z" },
                                    { name: "right", label: qsTr("Align right"), key: "Alt+→", needs: 2,
                                      icon: "M17 3 V17 M4 5.5 H14 V9 H4 Z M8.5 11 H14 V14.5 H8.5 Z" },
                                    { name: "top", label: qsTr("Align top"), key: "Alt+↑", needs: 2,
                                      icon: "M3 3 H17 M5.5 6 V16 H9 V6 Z M11 6 V11.5 H14.5 V6 Z" },
                                    { name: "middle", label: qsTr("Center vertically"), key: "Alt+V", needs: 2,
                                      icon: "M3 10 H17 M5.5 4.5 V15.5 H9 V4.5 Z M11 7 V13 H14.5 V7 Z" },
                                    { name: "bottom", label: qsTr("Align bottom"), key: "Alt+↓", needs: 2,
                                      icon: "M3 17 H17 M5.5 4 V14 H9 V4 Z M11 8.5 V14 H14.5 V8.5 Z" },
                                    { name: "spreadAcross", label: qsTr("Distribute horizontally"), key: "Alt+Shift+H", needs: 3,
                                      icon: "M3 3 V17 M17 3 V17 M6 6 H8.5 V14 H6 Z M11.5 6 H14 V14 H11.5 Z" },
                                    { name: "spreadDown", label: qsTr("Distribute vertically"), key: "Alt+Shift+V", needs: 3,
                                      icon: "M3 3 H17 M3 17 H17 M6 6 V8.5 H14 V6 Z M6 11.5 V14 H14 V11.5 Z" },
                                    { name: "front", label: qsTr("Bring to front"), key: "]", needs: 1,
                                      icon: "M8 8 H17 V17 H8 Z M3 12 V3 H12" },
                                    { name: "back", label: qsTr("Send to back"), key: "[", needs: 1,
                                      icon: "M3 3 H12 V12 H3 Z M17 8 V17 H8" },
                                    { name: "line", separator: true },
                                    { name: "group", label: qsTr("Group"), key: "Ctrl+G",
                                      icon: "M3 3 H17 V17 H3 Z M6 6 H10.5 V10.5 H6 Z M12 12 H14 V14 H12 Z" },
                                    { name: "ungroup", label: qsTr("Ungroup"), key: "Ctrl+Shift+G",
                                      icon: "M3 3 H10.5 V10.5 H3 Z M12.5 12.5 H17 V17 H12.5 Z" }
                                ]
                                delegate: Rectangle {
                                    id: arrangeRow
                                    required property var modelData
                                    readonly property bool separator: modelData.separator === true
                                    // A group is lined up and spaced as one
                                    // thing; the order moves whatever is selected.
                                    readonly property bool usable: separator ? false
                                        : modelData.name === "group" ? board.canGroup
                                        : modelData.name === "ungroup" ? board.canUngroup
                                        : modelData.needs === 1 ? board.selectionCount >= 1
                                        : board.arrangeCount >= modelData.needs
                                    objectName: "arrange-" + modelData.name
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: separator ? 0 : arrangeLine.implicitWidth + 20
                                    Layout.preferredHeight: separator ? 9 : 32
                                    radius: Math.min(4, theme.radius)
                                    color: usable && arrangeRowHover.hovered ? theme.colors.selection : "transparent"
                                    opacity: usable || separator ? 1 : 0.4

                                    Rectangle {
                                        visible: arrangeRow.separator
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 1
                                        color: theme.colors.selection
                                    }

                                    RowLayout {
                                        id: arrangeLine
                                        visible: !arrangeRow.separator
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 10

                                        Shape {
                                            Layout.preferredWidth: 20
                                            Layout.preferredHeight: 20
                                            preferredRendererType: Shape.CurveRenderer
                                            ShapePath {
                                                strokeColor: theme.foreground
                                                strokeWidth: 1.5
                                                fillColor: "transparent"
                                                capStyle: ShapePath.RoundCap
                                                joinStyle: ShapePath.RoundJoin
                                                PathSvg { path: arrangeRow.modelData.icon || "" }
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: arrangeRow.modelData.label || ""
                                        }
                                        Label {
                                            Layout.leftMargin: 14
                                            text: arrangeRow.modelData.key || ""
                                            opacity: 0.6
                                        }
                                    }
                                    HoverHandler { id: arrangeRowHover }
                                    TapHandler {
                                        enabled: arrangeRow.usable
                                        onTapped: {
                                            const name = arrangeRow.modelData.name
                                            if (name === "front")
                                                board.bringToFront()
                                            else if (name === "back")
                                                board.sendToBack()
                                            else if (name === "group")
                                                board.groupSelection()
                                            else if (name === "ungroup")
                                                board.ungroupSelection()
                                            else
                                                board.alignSelection(name)
                                            arrangeMenu.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // How far in the canvas is.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            width: status.implicitWidth + 24
            height: 34
            radius: theme.radius
            color: win.panel
            border.color: theme.colors.selection
            Label {
                id: status
                objectName: "status"
                anchors.centerIn: parent
                // Reads the language, so the number is written anew on a switch.
                text: translator.language, qsTr("%1%").arg(Math.round(view.zoom * 100).toLocaleString(Qt.locale(), 'f', 0))
            }
        }

        // Which board this is, and the way to the others.
        Rectangle {
            id: boardButton
            objectName: "boardButton"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 20
            width: Math.min(boardName.implicitWidth, 260) + 46
            height: 34
            radius: theme.radius
            color: boardMenu.visible || boardHover.hovered
                   ? Qt.tint(win.panel, Qt.alpha(theme.foreground, 0.06)) : win.panel
            border.color: theme.colors.selection

            Label {
                id: boardName
                objectName: "boardName"
                anchors.left: parent.left
                anchors.right: boardChevron.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                elide: Text.ElideRight
                text: backend.currentName
            }
            Shape {
                id: boardChevron
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 12
                width: 8
                height: 6
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: theme.foreground
                    strokeWidth: 1.4
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: "M1 1.5 L4 4.5 L7 1.5" }
                }
            }
            HoverHandler { id: boardHover }
            TapHandler { onTapped: boardMenu.visible ? boardMenu.close() : boardMenu.open() }

            Popup {
                id: boardMenu
                objectName: "boardMenu"
                x: win.rightToLeft ? parent.width - width : 0
                y: parent.height + 6
                width: 260
                padding: 8
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                // The question about deleting a board waits until this menu
                // is gone, or the keyboard stays with the menu as it closes
                // and Escape never reaches the question.
                property var pendingDelete: null
                onClosed: {
                    win.renamingId = ""
                    const doomed = pendingDelete
                    pendingDelete = null
                    if (doomed)
                        win.askToDelete(doomed.id, doomed.name)
                }
                background: Rectangle {
                    color: win.panel
                    border.color: theme.colors.selection
                    radius: theme.radius
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2
                    // An open popup keeps the keys to itself, so the key that
                    // opened it closes it from in here.
                    focus: true
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_B && !win.typing) {
                            boardMenu.close()
                            event.accepted = true
                        }
                    }
                    // A popup is not an Item and doesn't inherit the window's
                    // mirroring, so its content opts in.
                    LayoutMirroring.enabled: win.rightToLeft
                    LayoutMirroring.childrenInherit: true

                    ListView {
                        id: boardList
                        objectName: "boardList"
                        Layout.fillWidth: true
                        // As tall as its boards, until the window runs out.
                        Layout.preferredHeight: Math.min(contentHeight, win.height - 200)
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: backend.boards
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool current: modelData.id === backend.currentId
                            readonly property bool renaming: win.renamingId === modelData.id
                            objectName: "board-" + index
                            width: boardList.width
                            height: 34
                            radius: Math.min(4, theme.radius)
                            color: current ? theme.colors.selection
                                 : rowHover.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"

                            HoverHandler { id: rowHover }
                            TapHandler {
                                enabled: !row.renaming
                                // A tap on the pencil or the cross is theirs
                                // alone; anywhere else on the row opens the board.
                                onTapped: (point) => {
                                    // Judged by where the tap was, not by whether
                                    // they still show: the pencil's own tap has
                                    // already hidden them by the time this runs.
                                    if (rowActions.contains(rowActions.mapFromItem(row, point.position)))
                                        return
                                    backend.open(row.modelData.id)
                                    boardMenu.close()
                                }
                            }

                            Label {
                                visible: !row.renaming
                                anchors.left: parent.left
                                anchors.right: rowActions.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10
                                elide: Text.ElideRight
                                text: row.modelData.name
                            }
                            TextInput {
                                id: nameField
                                visible: row.renaming
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                clip: true
                                color: theme.foreground
                                font: status.font
                                selectionColor: theme.accent
                                selectedTextColor: theme.accentForeground
                                function begin() {
                                    text = row.modelData.name
                                    forceActiveFocus()
                                    selectAll()
                                }
                                onVisibleChanged: if (visible) begin()
                                Component.onCompleted: if (visible) begin()
                                // Leaving the field keeps the name as it was.
                                function finish(keep) {
                                    const id = row.modelData.id, name = text
                                    win.renamingId = ""
                                    if (keep)
                                        backend.rename(id, name)
                                }
                                onAccepted: finish(true)
                                Keys.onEscapePressed: finish(false)
                                onActiveFocusChanged: if (!activeFocus && row.renaming) win.renamingId = ""
                            }
                            Row {
                                id: rowActions
                                visible: rowHover.hovered && !row.renaming
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.rightMargin: 4

                                Item {
                                    objectName: "rename-" + row.index
                                    width: 26
                                    height: 26
                                    Shape {
                                        anchors.centerIn: parent
                                        width: 14
                                        height: 14
                                        preferredRendererType: Shape.CurveRenderer
                                        ShapePath {
                                            strokeColor: renameHover.hovered ? theme.foreground : theme.colors.muted
                                            strokeWidth: 1.3
                                            fillColor: "transparent"
                                            capStyle: ShapePath.RoundCap
                                            joinStyle: ShapePath.RoundJoin
                                            PathSvg { path: "M2 12 L2.6 9.2 L9.8 2 L12 4.2 L4.8 11.4 Z M8.4 3.4 L10.6 5.6" }
                                        }
                                    }
                                    HoverHandler { id: renameHover }
                                    TapHandler { onTapped: win.renamingId = row.modelData.id }
                                    ToolTip.visible: renameHover.hovered
                                    ToolTip.delay: 500
                                    ToolTip.text: qsTr("Rename this board")
                                }
                                Label {
                                    objectName: "delete-" + row.index
                                    width: 26
                                    height: 26
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    text: "×"
                                    font.pixelSize: 18
                                    color: removeHover.hovered ? theme.colors.red : theme.colors.muted
                                    HoverHandler { id: removeHover }
                                    TapHandler {
                                        onTapped: {
                                            boardMenu.pendingDelete = { id: row.modelData.id, name: row.modelData.name }
                                            boardMenu.close()
                                        }
                                    }
                                    ToolTip.visible: removeHover.hovered
                                    ToolTip.delay: 500
                                    ToolTip.text: qsTr("Delete this board")
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        color: theme.colors.selection
                    }

                    Rectangle {
                        id: newRow
                        objectName: "newBoard"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: Math.min(4, theme.radius)
                        color: newHover.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"

                        Label {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            text: qsTr("New board")
                            color: theme.accent
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 10
                            text: "Ctrl+N"
                            opacity: 0.6
                        }
                        HoverHandler { id: newHover }
                        TapHandler {
                            onTapped: {
                                backend.newBoard()
                                boardMenu.close()
                            }
                        }
                    }
                }
            }
        }

        // The board as a file, in Downloads.
        Row {
            id: exports
            anchors.left: boardButton.right
            anchors.top: boardButton.top
            anchors.leftMargin: 8
            spacing: 8

            Repeater {
                model: ["PDF", "SVG"]
                delegate: Rectangle {
                    id: exportButton
                    required property string modelData
                    readonly property bool usable: board.count > 0
                    objectName: "export-" + modelData.toLowerCase()
                    width: exportLabel.implicitWidth + 24
                    height: 34
                    radius: theme.radius
                    color: usable && exportHover.hovered
                           ? Qt.tint(win.panel, Qt.alpha(theme.foreground, 0.06)) : win.panel
                    border.color: theme.colors.selection

                    Label {
                        id: exportLabel
                        anchors.centerIn: parent
                        text: exportButton.modelData
                        opacity: exportButton.usable ? 1 : 0.4
                    }
                    HoverHandler { id: exportHover }
                    TapHandler {
                        enabled: exportButton.usable
                        onTapped: win.download(exportButton.modelData.toLowerCase())
                    }
                    ToolTip.visible: exportHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Save this board to Downloads as %1").arg(modelData)
                }
            }
        }

        // Where the file went, or what went wrong.
        Rectangle {
            objectName: "notice"
            visible: win.notice !== ""
            anchors.left: boardButton.left
            anchors.top: boardButton.bottom
            anchors.topMargin: 8
            width: noticeLabel.width + 24
            height: noticeLabel.implicitHeight + 16
            radius: theme.radius
            color: win.noticeFailed ? theme.colors.red : win.panel
            border.color: win.noticeFailed ? theme.colors.red : theme.accent
            Label {
                id: noticeLabel
                objectName: "noticeLabel"
                anchors.centerIn: parent
                width: Math.min(implicitWidth, win.width - 64)
                wrapMode: Text.WrapAnywhere
                color: win.noticeFailed ? "black" : theme.foreground
                text: win.notice
            }
        }
    }

    Popup {
        id: help
        objectName: "help"
        Overlay.modal: Rectangle { color: Qt.alpha("black", 0.45) }
        readonly property var entries: [
            { keys: "R  O  T  D  W  L", action: qsTr("Draw a rectangle, circle, triangle, rhombus, arrow or line") },
            { keys: "F", action: qsTr("Draw a frame; what lies inside it moves with it") },
            { keys: "P", action: qsTr("Stick a post-it") },
            { keys: "I", action: qsTr("Place an icon") },
            { keys: "M", action: qsTr("Add images from files") },
            { keys: "X · T T", action: qsTr("Write text") },
            { keys: "C", action: qsTr("Connect two shapes") },
            { keys: "V · Esc", action: qsTr("Select and move") },
            { keys: "Enter", action: qsTr("Write on the selected shape") },
            { keys: "1 – 8", action: qsTr("Color the selection") },
            { keys: "S", action: qsTr("Make the border solid, dashed or dotted") },
            { keys: "A", action: qsTr("Move a connection's arrow to its other end, or take it away") },
            { keys: "Alt", action: qsTr("Hold and drag a handle to rotate the shape") },
            { keys: "Shift", action: qsTr("Hold while drawing a line to keep it straight") },
            { keys: "Shift", action: qsTr("Hold while resizing to keep the shape's proportions") },
            { keys: ",  .", action: qsTr("Rotate the selection") },
            { keys: "Delete", action: qsTr("Delete the selection") },
            { keys: "Ctrl+C · Ctrl+V", action: qsTr("Copy and paste") },
            { keys: "Ctrl+D", action: qsTr("Duplicate") },
            { keys: "Ctrl+A", action: qsTr("Select everything") },
            { keys: "Ctrl+Z · Ctrl+Shift+Z", action: qsTr("Undo and redo") },
            { keys: "← ↑ ↓ →", action: qsTr("Nudge the selection") },
            { keys: "Alt+← ↑ ↓ →", action: qsTr("Align the selected shapes on that side") },
            { keys: "Alt+H · Alt+V", action: qsTr("Center the selected shapes on each other, across or up and down") },
            { keys: "Alt+Shift+H · V", action: qsTr("Space the selected shapes evenly, across or up and down") },
            { keys: "]  [", action: qsTr("Bring to the front or send to the back") },
            { keys: "Ctrl+G · Ctrl+Shift+G", action: qsTr("Group the selection, or take the group apart") },
            { keys: "+  −  0", action: qsTr("Zoom in, out, and back to 100%") },
            { keys: "Shift+F", action: qsTr("Fit everything in the window") },
            { keys: "Space", action: qsTr("Hold and drag to pan") },
            { keys: "B", action: qsTr("Show or hide the boards") },
            { keys: "Ctrl+N", action: qsTr("New board") },
            { keys: "Ctrl+Tab", action: qsTr("Next board") },
            { keys: "F2", action: qsTr("Rename this board") },
            { keys: "?", action: qsTr("Show or hide the shortcuts") },
            { keys: "Q", action: qsTr("Quit") }
        ]
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: 24
        height: Math.min(implicitHeight, win.height - 48)
        background: Rectangle {
            color: theme.background
            border.color: theme.colors.selection
            radius: theme.radius
        }

        contentItem: Flickable {
            implicitWidth: shortcuts.implicitWidth
            implicitHeight: shortcuts.implicitHeight
            contentHeight: shortcuts.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // A popup is not an Item and doesn't inherit the window's
            // mirroring, so its content opts in.
            GridLayout {
                id: shortcuts
                columns: 2
                columnSpacing: 24
                rowSpacing: 6
                LayoutMirroring.enabled: win.rightToLeft
                LayoutMirroring.childrenInherit: true

                Repeater {
                    model: help.entries
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData.keys
                        color: theme.accent
                        font.bold: true
                    }
                }
                Repeater {
                    model: help.entries
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        text: modelData.action
                    }
                }
            }
        }
    }

    Popup {
        id: confirm
        objectName: "confirm"
        Overlay.modal: Rectangle { color: Qt.alpha("black", 0.45) }
        property string boardId: ""
        property string boardName: ""
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: 24
        background: Rectangle {
            color: theme.background
            border.color: theme.colors.selection
            radius: theme.radius
        }

        ColumnLayout {
            spacing: 20
            LayoutMirroring.enabled: win.rightToLeft
            LayoutMirroring.childrenInherit: true

            Label {
                Layout.maximumWidth: 380
                wrapMode: Text.Wrap
                text: qsTr("Delete the board “%1”? It can't be brought back.").arg(confirm.boardName)
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                // Escape and Enter both land here: the answer that loses nothing.
                Button {
                    objectName: "keepBoard"
                    text: qsTr("Keep it")
                    flat: true
                    focus: true
                    Material.roundedScale: theme.radius
                    onClicked: confirm.close()
                }
                Button {
                    id: deleteButton
                    objectName: "deleteBoard"
                    text: qsTr("Delete")
                    highlighted: true
                    Material.roundedScale: theme.radius
                    onClicked: {
                        confirm.close()
                        backend.remove(confirm.boardId)
                    }
                    // A highlighted button colors its caption by Material's own
                    // rule, not Material.foreground, so it can't read on every accent.
                    contentItem: Label {
                        text: deleteButton.text
                        font: deleteButton.font
                        color: theme.accentForeground
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
