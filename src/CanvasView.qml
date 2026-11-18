import QtQuick
import QtQuick.Shapes
import Qt.labs.qmlmodels
import Slate

// The infinite canvas: a camera over the board's items, and every gesture
// made on it. Items live in world coordinates under one scaled item, so
// panning and zooming move a single transform; handles and guides are drawn
// over it in screen coordinates, so they stay the same size at any zoom.
Item {
    id: view
    clip: true
    focus: true

    property string tool: "select"
    // The icon the icon tool places.
    property string iconName: "star"
    property real camX: 0
    property real camY: 0
    property real zoom: 1
    readonly property real minZoom: 0.05
    readonly property real maxZoom: 8

    property bool spaceHeld: false
    property point pointer: Qt.point(width / 2, height / 2)
    property bool pointerInside: false

    // What the press in progress is doing: pan, move, band, draw, text,
    // resize, rotate, endpoint, or connect.
    property string mode: ""
    property point pressAt
    property point lastAt
    property point pressWorld
    property bool moved: false
    property int drawId: 0
    property var activeHandle: null
    property rect startBox
    property int startFont: 24
    property real startRotation: 0
    property real startAngle: 0
    property bool altHeld: false
    property bool rotateCaught: false
    // The angle shown beside the pointer while something is turned or a
    // line is drawn, and whether it has caught on a step.
    property real shownAngle: 0
    property point grabOffset
    property point startA
    property point startB
    property bool bandAdd: false
    property rect bandRect
    // Where the selection was when its drag began, and the lines it is
    // lined up on now.
    property rect moveStart
    property var guides: []

    property int hoverId: 0
    property string hoverEdge: ""
    property real hoverEdgeRotation: 0
    property var hoverHandle: null
    property rect hoverBox
    property real hoverRotation: 0
    property int sourceId: 0
    property int targetId: 0
    property rect targetBox
    property real targetRotation: 0

    property int editingId: 0
    property string editingKind: ""
    property rect editBox
    property real editRotation: 0
    readonly property bool editing: editingId !== 0

    property bool restoring: false
    property bool pendingCenter: false

    function toWorld(p) { return Qt.point((p.x - camX) / zoom, (p.y - camY) / zoom) }
    function toScreen(p) { return Qt.point(p.x * zoom + camX, p.y * zoom + camY) }
    function sx(x) { return x * zoom + camX }
    function sy(y) { return y * zoom + camY }

    // Where something new lands: under the pointer when it is over the
    // canvas, in the middle of what is visible otherwise.
    function insertPoint() { return toWorld(pointerInside ? pointer : Qt.point(width / 2, height / 2)) }
    function focusPoint() { return pointerInside ? pointer : Qt.point(width / 2, height / 2) }

    function inkOf(key) { return key ? theme.colors[key] : theme.foreground }

    // A post-it is yellow until it is given another color, and its words are
    // black or white, whichever reads on the paper.
    function paperOf(key) { return theme.colors[key || "yellow"] }
    function readable(c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b > 0.55 ? "black" : "white" }

    // A point turned about another, by degrees.
    function spin(px, py, cx, cy, degrees) {
        const r = degrees * Math.PI / 180, c = Math.cos(r), s = Math.sin(r)
        return Qt.point(cx + (px - cx) * c - (py - cy) * s, cy + (px - cx) * s + (py - cy) * c)
    }

    // --- Camera ------------------------------------------------------------

    function clampZoom(z) { return Math.max(minZoom, Math.min(maxZoom, z)) }

    function setZoomAround(pt, z) {
        const w = toWorld(pt)
        zoom = z
        camX = pt.x - w.x * z
        camY = pt.y - w.y * z
    }

    // A wheel notch or a key glides to its zoom; a pinch or a touchpad
    // scroll already arrives in small steps and is applied as it comes.
    property point zoomAnchor
    property real zoomDrive: 1
    onZoomDriveChanged: if (zoomAnim.running) setZoomAround(zoomAnchor, zoomDrive)
    NumberAnimation {
        id: zoomAnim
        target: view
        property: "zoomDrive"
        duration: 140
        easing.type: Easing.OutCubic
    }

    function zoomAt(pt, factor, animate) {
        if (!animate) {
            zoomAnim.stop()
            setZoomAround(pt, clampZoom(zoom * factor))
            return
        }
        const target = clampZoom((zoomAnim.running ? zoomAnim.to : zoom) * factor)
        zoomAnim.stop()
        zoomAnchor = pt
        zoomAnim.from = zoom
        zoomAnim.to = target
        zoomAnim.start()
    }

    function resetZoom() { zoomAt(Qt.point(width / 2, height / 2), 1 / zoom, true) }

    function fit() {
        zoomAnim.stop()
        const b = board.contentBounds()
        if (board.count === 0) {
            zoom = 1
            camX = width / 2
            camY = height / 2
            return
        }
        const z = clampZoom(Math.min((width - 160) / Math.max(b.width, 1), (height - 220) / Math.max(b.height, 1), 2))
        zoom = z
        camX = width / 2 - (b.x + b.width / 2) * z
        camY = height / 2 - (b.y + b.height / 2) * z
    }

    // Each board remembers where it was looked at. A new one starts with its
    // origin in the middle, once the window has a size to find the middle of.
    function restoreView() {
        zoomAnim.stop()
        restoring = true
        const saved = board.hasView, x = board.viewX, y = board.viewY, z = board.viewZoom
        mode = ""
        hoverId = 0
        editingId = 0
        zoom = saved ? clampZoom(z) : 1
        camX = saved ? x : 0
        camY = saved ? y : 0
        pendingCenter = !saved
        restoring = false
        center()
    }

    function center() {
        if (!pendingCenter || width <= 0 || height <= 0)
            return
        pendingCenter = false
        camX = width / 2
        camY = height / 2
    }

    function saveView() {
        if (!restoring && !pendingCenter)
            board.setView(camX, camY, zoom)
    }

    onCamXChanged: saveView()
    onCamYChanged: saveView()
    onZoomChanged: saveView()
    onWidthChanged: center()
    onHeightChanged: center()
    Component.onCompleted: restoreView()

    Connections {
        target: board
        function onLoaded() { view.restoreView() }
    }

    // --- Editing text --------------------------------------------------------

    function beginEdit(id) {
        if (editing)
            commitEdit()
        const it = board.item(id)
        if (!it.kind || it.kind === "line" || it.kind === "connector" || it.kind === "icon")
            return
        board.checkpoint()
        board.selectOnly(id)
        editingKind = it.kind
        editBox = Qt.rect(it.x, it.y, it.w, it.h)
        editRotation = it.rotation
        editor.font.pixelSize = it.kind === "text" ? it.fontSize : 18
        editor.text = it.text
        editingId = id
        recolorEditor()
        editor.forceActiveFocus()
        editor.selectAll()
    }

    // The words being typed take the color of what they are written on,
    // and change with it when it is recolored mid-sentence.
    function recolorEditor() {
        if (!editing)
            return
        const it = board.item(editingId)
        editor.color = it.kind === "text" ? inkOf(it.color)
                     : it.kind === "postit" ? readable(paperOf(it.color)) : theme.foreground
    }
    Connections {
        target: board
        function onShownColorChanged() { view.recolorEditor() }
    }

    function commitEdit() {
        if (!editing)
            return
        const id = editingId, kind = editingKind
        editingId = 0
        // Text with nothing in it is nothing at all.
        if (kind === "text" && editor.text.trim() === "")
            board.removeItem(id)
        view.forceActiveFocus()
    }

    // --- Pointer -------------------------------------------------------------

    readonly property var handleList: {
        if (mode === "move" || editing || tool !== "select")
            return []
        // Several things selected are sized together, by the box around them.
        if (!board.selectedId) {
            if (board.groupCount < 2)
                return []
            const g = board.groupBounds
            const l = sx(g.x) - 3, t = sy(g.y) - 3, r = sx(g.x + g.width) + 3, bt = sy(g.y + g.height) + 3
            const cx = (l + r) / 2, cy = (t + bt) / 2
            const group = [{ x: l, y: t, corner: 0, group: true }, { x: r, y: t, corner: 1, group: true },
                           { x: l, y: bt, corner: 2, group: true }, { x: r, y: bt, corner: 3, group: true }]
            if (r - l >= 44)
                group.push({ x: cx, y: t, edge: "t", group: true }, { x: cx, y: bt, edge: "b", group: true })
            if (bt - t >= 44)
                group.push({ x: l, y: cy, edge: "l", group: true }, { x: r, y: cy, edge: "r", group: true })
            return group
        }
        const k = board.selectedKind
        if (k === "connector")
            return []
        if (k === "line")
            return [{ x: sx(board.selectionStart.x), y: sy(board.selectionStart.y), end: 0 },
                    { x: sx(board.selectionEnd.x), y: sy(board.selectionEnd.y), end: 1 }]
        // The corners of the shape as it is turned. Dragged, a handle
        // resizes; dragged with Alt held, it turns the shape instead.
        const b = board.selectedBox, rot = board.selectedRotation
        const l = sx(b.x) - 3, t = sy(b.y) - 3, r = sx(b.x + b.width) + 3, bt = sy(b.y + b.height) + 3
        const cx = (l + r) / 2, cy = (t + bt) / 2
        const at = (x, y, more) => {
            const p = spin(x, y, cx, cy, rot)
            return Object.assign({ x: p.x, y: p.y, rot: rot }, more)
        }
        // Text is as large as its words, so one handle scales the type.
        if (k === "text")
            return [at(r, bt, { corner: 3 })]
        const handles = [at(l, t, { corner: 0 }), at(r, t, { corner: 1 }),
                         at(l, bt, { corner: 2 }), at(r, bt, { corner: 3 })]
        // And one in the middle of each side, which resizes that way only.
        // A side too short to fit one between its corners goes without; the
        // side itself can still be dragged.
        if (r - l >= 44)
            handles.push(at(cx, t, { edge: "t" }), at(cx, bt, { edge: "b" }))
        if (bt - t >= 44)
            handles.push(at(l, cy, { edge: "l" }), at(r, cy, { edge: "r" }))
        return handles
    }

    // Hovering a shape shows a dot on each side; dragging from one connects.
    readonly property var dotList: {
        if (!hoverId || mode !== "" || tool !== "select" || editing)
            return []
        // Off the middle of each side of the shape as it is turned, so they
        // stay with its sides and its handles.
        const b = hoverBox, o = 16
        const l = sx(b.x), t = sy(b.y), r = sx(b.x + b.width), bt = sy(b.y + b.height)
        const cx = (l + r) / 2, cy = (t + bt) / 2
        return [spin(cx, t - o, cx, cy, hoverRotation), spin(r + o, cy, cx, cy, hoverRotation),
                spin(cx, bt + o, cx, cy, hoverRotation), spin(l - o, cy, cx, cy, hoverRotation)]
    }

    // Where a line from `from` to the pointer ends. With Shift held it is
    // kept to every eighth of a turn (level, upright, or a diagonal), the
    // end sliding along that direction as the pointer moves. Answers the
    // end, and the line's angle as it reads on screen: 0 level, 90 upright.
    function lineEnd(from, to, straight) {
        let end = to
        if (straight) {
            const step = Math.PI / 4
            const dir = Math.round(Math.atan2(to.y - from.y, to.x - from.x) / step) * step
            const ux = Math.cos(dir), uy = Math.sin(dir)
            const length = (to.x - from.x) * ux + (to.y - from.y) * uy
            // Level and upright lines come out exact, not a hair off.
            end = Qt.point(from.x + Math.round(ux * 1e6) / 1e6 * length,
                           from.y + Math.round(uy * 1e6) / 1e6 * length)
        }
        const degrees = -Math.atan2(end.y - from.y, end.x - from.x) * 180 / Math.PI
        return { x: end.x, y: end.y, angle: ((Math.round(degrees) % 180) + 180) % 180 }
    }

    function nearest(list, p, reach) {
        for (const h of list)
            if (Math.hypot(h.x - p.x, h.y - p.y) <= reach)
                return h
        return null
    }

    function boxOf(id) { return board.boundsOf(id) }

    // The shape hovered, or aimed at with a connection, as it is before it
    // is turned, and how far it is turned.
    function setBox(which, id) {
        const it = board.item(id)
        view[which + "Box"] = Qt.rect(it.x, it.y, it.w, it.h)
        view[which + "Rotation"] = it.rotation
    }

    // The side of the selected shape a point is on: l, r, t, or b, as the
    // shape has them before it is turned. Dragging a side resizes the shape
    // that way only. Text is as large as its words and a line has only ends,
    // so neither has sides to drag.
    function edgeAt(pt) {
        if (!board.selectedId || tool !== "select" || editing)
            return ""
        const k = board.selectedKind
        if (k === "connector" || k === "line" || k === "text")
            return ""
        const b = board.selectedBox, w = toWorld(pt), reach = 7 / zoom
        const p = spin(w.x, w.y, b.x + b.width / 2, b.y + b.height / 2, -board.selectedRotation)
        const alongX = p.x >= b.x - reach && p.x <= b.x + b.width + reach
        const alongY = p.y >= b.y - reach && p.y <= b.y + b.height + reach
        if (alongY && Math.abs(p.x - b.x) <= reach) return "l"
        if (alongY && Math.abs(p.x - b.x - b.width) <= reach) return "r"
        if (alongX && Math.abs(p.y - b.y) <= reach) return "t"
        if (alongX && Math.abs(p.y - b.y - b.height) <= reach) return "b"
        return ""
    }

    // The side a drag from a point would resize, and of which shape: a side
    // of the selected shape's box, or the outline of any shape under the
    // pointer, selected or not. Several shapes selected together are moved by
    // their outlines like anywhere else on them.
    function sideAt(pt) {
        if (tool !== "select" || editing)
            return null
        const own = edgeAt(pt)
        if (own)
            return { id: board.selectedId, edge: own, rotation: board.selectedRotation }
        const w = toWorld(pt), reach = 7 / zoom
        const id = board.itemAt(w.x, w.y, reach)
        if (!id || (board.selectionCount > 1 && board.isSelected(id)))
            return null
        const side = board.outlineSide(id, w.x, w.y, reach)
        return side ? { id: id, edge: side, rotation: board.item(id).rotation } : null
    }

    // The pointer says what a drag from here would do. A resize cursor
    // points the way the side or corner under it moves, which for a turned
    // shape is not the way it would on an upright one: the arrow is picked
    // from the four there are by the direction it has to show.
    function resizeCursor(degrees) {
        const a = ((degrees % 180) + 180) % 180
        return a < 22.5 || a >= 157.5 ? Qt.SizeHorCursor
             : a < 67.5 ? Qt.SizeFDiagCursor
             : a < 112.5 ? Qt.SizeVerCursor
             : Qt.SizeBDiagCursor
    }
    function edgeCursor(edge, rotation) {
        return resizeCursor(rotation + (edge === "t" || edge === "b" ? 90 : 0))
    }
    function handleCursor(handle) {
        if (handle.end !== undefined)
            return Qt.SizeAllCursor
        if (handle.edge)
            return edgeCursor(handle.edge, board.selectedRotation)
        return resizeCursor(board.selectedRotation + (handle.corner === 0 || handle.corner === 3 ? 45 : 135))
    }

    function updateHover(p) {
        if (tool !== "select" || editing || !pointerInside) {
            hoverId = 0
            hoverEdge = ""
            hoverHandle = null
            return
        }
        hoverHandle = nearest(handleList, pointer, 10)
        const over = hoverHandle ? null : sideAt(pointer)
        hoverEdge = over ? over.edge : ""
        hoverEdgeRotation = over ? over.rotation : 0
        // Reaching for a dot leaves the shape, so the dots stay while the
        // pointer is near it.
        hoverId = board.shapeAt(p.x, p.y, 28 / zoom)
        if (hoverId)
            setBox("hover", hoverId)
    }

    function press(m) {
        if (editing)
            commitEdit()
        view.forceActiveFocus()
        pointer = Qt.point(m.x, m.y)
        pressAt = pointer
        lastAt = pointer
        moved = false
        const p = toWorld(pointer)
        pressWorld = p
        if (m.button !== Qt.LeftButton || spaceHeld) {
            mode = "pan"
            return
        }
        const shift = (m.modifiers & Qt.ShiftModifier) !== 0
        if (tool === "select") {
            const handle = nearest(handleList, pointer, 10)
            if (handle && handle.group) {
                const g = board.groupBounds, c = handle.corner, side = handle.edge || ""
                // The spot taken hold of on the box follows the pointer from
                // where it was, as on a single shape.
                const hx = side === "l" || (!side && !(c & 1)) ? g.x : side === "r" || (!side && (c & 1)) ? g.x + g.width : p.x
                const hy = side === "t" || (!side && !(c & 2)) ? g.y : side === "b" || (!side && (c & 2)) ? g.y + g.height : p.y
                grabOffset = Qt.point(hx - p.x, hy - p.y)
                activeHandle = handle
                startBox = g
                board.beginScale()
                mode = "scale"
                return
            }
            // Alt on any handle of a shape turns it about its middle, from
            // the angle it was taken hold of at.
            if (handle && handle.end === undefined && (m.modifiers & Qt.AltModifier)) {
                activeHandle = handle
                startBox = board.selectedBox
                startRotation = board.selectedRotation
                startAngle = Math.atan2(p.y - (startBox.y + startBox.height / 2),
                                        p.x - (startBox.x + startBox.width / 2))
                mode = "rotate"
                return
            }
            if (handle && !handle.edge) {
                activeHandle = handle
                startBox = board.selectedBox
                startRotation = board.selectedRotation
                startA = board.selectionStart
                startB = board.selectionEnd
                startFont = board.item(board.selectedId).fontSize
                mode = handle.end !== undefined ? "endpoint" : "resize"
                // A handle sits just outside its corner; the corner follows
                // the pointer from where it was, without jumping to it.
                if (mode === "resize") {
                    const b = startBox, c = handle.corner
                    const corner = spin((c & 1) ? b.x + b.width : b.x, (c & 2) ? b.y + b.height : b.y,
                                        b.x + b.width / 2, b.y + b.height / 2, startRotation)
                    grabOffset = Qt.point(corner.x - p.x, corner.y - p.y)
                }
                return
            }
            // A side of a shape that isn't selected yet selects it and
            // resizes it in the one drag; with Shift it is a click that adds
            // to the selection, as anywhere else on the shape.
            const over = handle ? { id: board.selectedId, edge: handle.edge } : sideAt(pointer)
            const side = over && (over.id === board.selectedId || !shift) ? over.edge : ""
            if (side) {
                if (over.id !== board.selectedId)
                    board.selectOnly(over.id)
                // The grabbed spot on the side follows the pointer from where
                // it was, like a corner does.
                const b = board.selectedBox, rot = board.selectedRotation
                const cx = b.x + b.width / 2, cy = b.y + b.height / 2
                const local = spin(p.x, p.y, cx, cy, -rot)
                const held = spin(side === "l" ? b.x : side === "r" ? b.x + b.width : local.x,
                                  side === "t" ? b.y : side === "b" ? b.y + b.height : local.y, cx, cy, rot)
                grabOffset = Qt.point(held.x - p.x, held.y - p.y)
                activeHandle = { edge: side }
                startBox = b
                startRotation = rot
                mode = "resize"
                return
            }
            if (nearest(dotList, pointer, 11)) {
                sourceId = hoverId
                targetId = 0
                mode = "connect"
                return
            }
            const id = board.itemAt(p.x, p.y, 6 / zoom)
            if (id) {
                if (shift)
                    board.toggle(id)
                else if (!board.isSelected(id))
                    board.selectOnly(id)
                mode = "move"
            } else {
                if (!shift)
                    board.clearSelection()
                bandAdd = shift
                bandRect = Qt.rect(0, 0, 0, 0)
                mode = "band"
            }
        } else if (tool === "connect") {
            const id = board.shapeAt(p.x, p.y, 0)
            if (id) {
                sourceId = id
                targetId = 0
                mode = "connect"
            }
        } else if (tool === "text") {
            mode = "text"
        } else {
            drawId = 0
            mode = "draw"
        }
    }

    function drag(m) {
        altHeld = (m.modifiers & Qt.AltModifier) !== 0
        pointer = Qt.point(m.x, m.y)
        const p = toWorld(pointer)
        if (mode === "") {
            updateHover(p)
            return
        }
        // A press that barely moves is a click, not a drag.
        if (!moved && mode !== "pan" && Math.hypot(pointer.x - pressAt.x, pointer.y - pressAt.y) < 4)
            return
        const first = !moved
        moved = true
        const dx = pointer.x - lastAt.x, dy = pointer.y - lastAt.y
        lastAt = pointer
        const shift = (m.modifiers & Qt.ShiftModifier) !== 0

        if (mode === "pan") {
            camX += dx
            camY += dy
        } else if (mode === "move") {
            if (first) {
                board.checkpoint()
                moveStart = board.movableBounds()
            }
            // Where the pointer alone would put the selection, and from
            // there onto whatever it lines up with nearby. Ctrl lets it go
            // exactly where it is put.
            const wantX = moveStart.x + (pointer.x - pressAt.x) / zoom
            const wantY = moveStart.y + (pointer.y - pressAt.y) / zoom
            let snap = { dx: 0, dy: 0, guides: [] }
            if (!(m.modifiers & Qt.ControlModifier))
                snap = board.snapOffset(wantX, wantY, moveStart.width, moveStart.height, 6 / zoom)
            const now = board.movableBounds()
            board.moveSelection(wantX + snap.dx - now.x, wantY + snap.dy - now.y)
            guides = snap.guides
        } else if (mode === "band") {
            bandRect = Qt.rect(Math.min(pressAt.x, pointer.x), Math.min(pressAt.y, pointer.y),
                               Math.abs(pointer.x - pressAt.x), Math.abs(pointer.y - pressAt.y))
            board.selectInRect(pressWorld.x, pressWorld.y, p.x - pressWorld.x, p.y - pressWorld.y, bandAdd)
        } else if (mode === "draw") {
            if (!drawId)
                drawId = tool === "icon" ? board.addIcon(iconName, pressWorld.x, pressWorld.y, 0, 0)
                                         : board.add(tool, pressWorld.x, pressWorld.y, 0, 0)
            let w = p.x - pressWorld.x, h = p.y - pressWorld.y
            if (tool === "line") {
                const end = lineEnd(pressWorld, p, shift)
                board.setGeometry(drawId, pressWorld.x, pressWorld.y, end.x - pressWorld.x, end.y - pressWorld.y)
                shownAngle = end.angle
                rotateCaught = shift
            } else {
                // Shift keeps it square, or a true circle.
                if (shift) {
                    const side = Math.max(Math.abs(w), Math.abs(h))
                    w = w < 0 ? -side : side
                    h = h < 0 ? -side : side
                }
                board.setGeometry(drawId, Math.min(pressWorld.x, pressWorld.x + w),
                                  Math.min(pressWorld.y, pressWorld.y + h), Math.abs(w), Math.abs(h))
            }
        } else if (mode === "resize") {
            if (first)
                board.checkpoint()
            const id = board.selectedId, b = startBox, c = activeHandle.corner, rot = startRotation
            const side = activeHandle.edge || ""
            const cx = b.x + b.width / 2, cy = b.y + b.height / 2
            if (board.selectedKind === "text") {
                const local = spin(p.x + grabOffset.x, p.y + grabOffset.y, cx, cy, -rot)
                board.setFontSize(id, Math.round(startFont * Math.max(0.1, (local.y - b.y) / Math.max(b.height, 1))))
                return
            }
            // The opposite corner stays where it is on the canvas. Measured
            // from it along the shape's own turned sides, the pointer gives
            // the new size, and the new middle is halfway between the two.
            // A side is held by the middle of the side across from it, and
            // changes only the width or only the height.
            const wide = side === "l" || side === "r", tall = side === "t" || side === "b"
            const anchor = spin(tall ? cx : side === "l" || (!side && !(c & 1)) ? b.x + b.width : b.x,
                                wide ? cy : side === "t" || (!side && !(c & 2)) ? b.y + b.height : b.y, cx, cy, rot)
            const along = spin(p.x + grabOffset.x, p.y + grabOffset.y, anchor.x, anchor.y, -rot)
            let dx = along.x - anchor.x, dy = along.y - anchor.y
            // The side or corner being dragged catches on the sides and
            // middles of the shapes around it, with guides to show which, so
            // a shape can be made to end where its neighbors do. A turned
            // shape's sides run at an angle to all that and are left alone,
            // and Ctrl lets any shape be sized freely.
            //
            // Shift keeps the shape's proportions. From a corner, the way the
            // pointer has gone furthest sets the size and the other follows;
            // from a side, the other way grows with it, evenly about the
            // middle. Only the side that leads catches on its neighbors.
            const keep = shift && b.width > 0 && b.height > 0
            let useX = !tall, useY = !wide
            if (keep && !tall && !wide) {
                useX = Math.abs(dx) / b.width >= Math.abs(dy) / b.height
                useY = !useX
            }
            let lines = []
            if (rot === 0 && !(m.modifiers & Qt.ControlModifier)) {
                const snap = board.snapEdges(anchor.x + dx, anchor.y + dy, useX, useY, 6 / zoom)
                dx += snap.dx
                dy += snap.dy
                lines = snap.guides
            }
            guides = lines
            let w = tall ? b.width : Math.max(8, Math.abs(dx)), h = wide ? b.height : Math.max(8, Math.abs(dy))
            if (keep) {
                let scale = wide ? w / b.width : tall ? h / b.height
                          : useX ? Math.abs(dx) / b.width : Math.abs(dy) / b.height
                scale = Math.max(scale, 8 / Math.min(b.width, b.height))
                w = b.width * scale
                h = b.height * scale
            }
            const mid = spin(anchor.x + (tall ? 0 : (dx < 0 ? -w : w) / 2),
                             anchor.y + (wide ? 0 : (dy < 0 ? -h : h) / 2), anchor.x, anchor.y, rot)
            board.setGeometry(id, mid.x - w / 2, mid.y - h / 2, w, h)
        } else if (mode === "scale") {
            if (first)
                board.checkpoint()
            const b = startBox, c = activeHandle.corner, side = activeHandle.edge || ""
            const wide = side === "l" || side === "r", tall = side === "t" || side === "b"
            // The side or corner across from the one held stays put; the box
            // never turns inside out, only down to a sliver.
            const leftHeld = side === "l" || (!side && !(c & 1)), topHeld = side === "t" || (!side && !(c & 2))
            const ax = leftHeld ? b.x + b.width : b.x, ay = topHeld ? b.y + b.height : b.y
            const dirX = leftHeld ? -1 : 1, dirY = topHeld ? -1 : 1
            let dx = p.x + grabOffset.x - ax, dy = p.y + grabOffset.y - ay
            const keep = shift && b.width > 0 && b.height > 0
            let useX = !tall, useY = !wide
            if (keep && !tall && !wide) {
                useX = Math.abs(dx) / b.width >= Math.abs(dy) / b.height
                useY = !useX
            }
            let lines = []
            if (!(m.modifiers & Qt.ControlModifier)) {
                const snap = board.snapEdges(ax + dx, ay + dy, useX, useY, 6 / zoom)
                dx += snap.dx
                dy += snap.dy
                lines = snap.guides
            }
            guides = lines
            let w = tall ? b.width : Math.max(8, dx * dirX), h = wide ? b.height : Math.max(8, dy * dirY)
            if (keep) {
                let scale = wide ? w / b.width : tall ? h / b.height : useX ? w / b.width : h / b.height
                scale = Math.max(scale, 8 / Math.max(1, Math.min(b.width, b.height)))
                w = b.width * scale
                h = b.height * scale
            }
            // From a side, the other way stays centred where it was.
            const x = tall ? b.x + (b.width - w) / 2 : dirX > 0 ? ax : ax - w
            const y = wide ? b.y + (b.height - h) / 2 : dirY > 0 ? ay : ay - h
            board.scaleSelectionTo(x, y, w, h)
        } else if (mode === "rotate") {
            if (first)
                board.checkpoint()
            // The shape turns by as much as the pointer has gone round its
            // middle. Shift turns in steps of 15 degrees.
            const b = startBox
            const swept = Math.atan2(p.y - (b.y + b.height / 2), p.x - (b.x + b.width / 2)) - startAngle
            let angle = startRotation + swept * 180 / Math.PI
            // Turning catches like moving does: on every eighth of a turn
            // (level, upright, and the diagonals between) and on the angle of
            // any other shape, once it is within five degrees. Ctrl lets it
            // turn freely.
            let caught = shift
            if (shift) {
                angle = Math.round(angle / 15) * 15
            } else if (!(m.modifiers & Qt.ControlModifier)) {
                const apart = (a, b) => Math.abs(((a - b) % 360 + 540) % 360 - 180)
                let nearest = Math.round(angle / 45) * 45, gap = apart(angle, nearest)
                for (const other of board.otherRotations()) {
                    if (apart(angle, other) < gap) {
                        nearest = other
                        gap = apart(angle, other)
                    }
                }
                caught = gap <= 5
                if (caught)
                    angle = nearest
            }
            rotateCaught = caught
            board.setRotation(board.selectedId, caught ? angle : Math.round(angle * 100) / 100)
            shownAngle = board.selectedRotation
        } else if (mode === "endpoint") {
            if (first)
                board.checkpoint()
            // The end held turns about the other one, kept straight with Shift.
            const fixed = activeHandle.end === 0 ? startB : startA
            const end = lineEnd(fixed, p, shift)
            if (activeHandle.end === 0)
                board.setGeometry(board.selectedId, end.x, end.y, fixed.x - end.x, fixed.y - end.y)
            else
                board.setGeometry(board.selectedId, fixed.x, fixed.y, end.x - fixed.x, end.y - fixed.y)
            shownAngle = end.angle
            rotateCaught = shift
        } else if (mode === "connect") {
            const id = board.shapeAt(p.x, p.y, 0)
            targetId = id === sourceId ? 0 : id
            if (targetId)
                setBox("target", targetId)
        }
    }

    function release(m) {
        pointer = Qt.point(m.x, m.y)
        const p = toWorld(pointer)
        const was = mode
        mode = ""
        guides = []
        if (was === "draw") {
            // A click drops the shape at a comfortable size around the pointer.
            if (!drawId && tool !== "line") {
                const w = tool === "rect" ? 160 : tool === "postit" ? 150 : tool === "icon" ? 64 : 120
                const h = tool === "rect" ? 100 : tool === "triangle" ? 110 : tool === "postit" ? 150
                        : tool === "icon" ? 64 : 120
                drawId = tool === "icon" ? board.addIcon(iconName, p.x - w / 2, p.y - h / 2, w, h)
                                         : board.add(tool, p.x - w / 2, p.y - h / 2, w, h)
            }
            // A post-it is for writing on, so it is ready to be typed into.
            const stuck = tool === "postit" ? drawId : 0
            drawId = 0
            tool = "select"
            if (stuck)
                beginEdit(stuck)
        } else if (was === "text") {
            const id = board.add("text", p.x, p.y - 15, 0, 0)
            tool = "select"
            beginEdit(id)
        } else if (was === "connect") {
            if (targetId) {
                board.link(sourceId, targetId)
            } else if (moved && Math.hypot(pointer.x - pressAt.x, pointer.y - pressAt.y) > 40
                       && !board.shapeAt(p.x, p.y, 0)) {
                const id = board.linkToNew(sourceId, p.x, p.y)
                if (id)
                    beginEdit(id)
            }
            sourceId = 0
            targetId = 0
        }
        updateHover(p)
    }

    function doubleClick(m) {
        if (m.button !== Qt.LeftButton || tool !== "select")
            return
        const p = toWorld(Qt.point(m.x, m.y))
        mode = ""
        const id = board.itemAt(p.x, p.y, 6 / zoom)
        if (id)
            beginEdit(id)
        else
            beginEdit(board.add("text", p.x, p.y - 15, 0, 0))
    }

    // Alt is followed here too, so the pointer over a handle changes the
    // moment the key goes down, not only once the mouse next moves.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceHeld = true
            event.accepted = true
        } else if (event.key === Qt.Key_Alt) {
            altHeld = true
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceHeld = false
            event.accepted = true
        } else if (event.key === Qt.Key_Alt) {
            altHeld = false
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus) {
            spaceHeld = false
            altHeld = false
        }
    }

    // --- The canvas ------------------------------------------------------------

    DotGrid {
        anchors.fill: parent
        offsetX: view.camX
        offsetY: view.camY
        // The grid halves or doubles as the zoom changes, so the dots are
        // never crowded and never sparse.
        step: {
            let s = 24 * view.zoom
            while (s < 14) s *= 2
            while (s > 56) s /= 2
            return s
        }
        dotSize: 2
        color: Qt.alpha(theme.foreground, theme.light ? 0.22 : 0.16)
    }

    Item {
        id: world
        x: view.camX
        y: view.camY
        scale: view.zoom
        transformOrigin: Item.TopLeft

        Repeater {
            model: board
            delegate: DelegateChooser {
                role: "kind"

                DelegateChoice {
                    roleValue: "rect"
                    Body {
                        id: rectBody
                        zoom: view.zoom
                        editingId: view.editingId
                        Rectangle {
                            anchors.fill: parent
                            z: -1
                            color: rectBody.paint
                            border.color: rectBody.ink
                            border.width: 2
                            radius: Math.min(theme.radius, width / 2, height / 2)
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "ellipse"
                    Body {
                        id: ellipseBody
                        zoom: view.zoom
                        editingId: view.editingId
                        Shape {
                            anchors.fill: parent
                            z: -1
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: ellipseBody.ink
                                strokeWidth: 2
                                fillColor: ellipseBody.paint
                                PathAngleArc {
                                    centerX: ellipseBody.width / 2
                                    centerY: ellipseBody.height / 2
                                    radiusX: Math.max(0, ellipseBody.width / 2 - 1)
                                    radiusY: Math.max(0, ellipseBody.height / 2 - 1)
                                    startAngle: 0
                                    sweepAngle: 360
                                }
                            }
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "triangle"
                    Body {
                        id: triangleBody
                        zoom: view.zoom
                        editingId: view.editingId
                        // The words sit in the wide part of the triangle.
                        labelOffset: height / 6
                        labelWidth: width / 2
                        Shape {
                            anchors.fill: parent
                            z: -1
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: triangleBody.ink
                                strokeWidth: 2
                                fillColor: triangleBody.paint
                                joinStyle: ShapePath.RoundJoin
                                startX: triangleBody.width / 2
                                startY: 1
                                PathLine { x: triangleBody.width - 1; y: triangleBody.height - 1 }
                                PathLine { x: 1; y: triangleBody.height - 1 }
                                PathLine { x: triangleBody.width / 2; y: 1 }
                            }
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "icon"
                    Body {
                        id: iconBody
                        required property string iconName
                        zoom: view.zoom
                        editingId: view.editingId
                        // Drawn square in the middle of its box, from the
                        // same 24 by 24 outline the menu and the files use.
                        Shape {
                            anchors.centerIn: parent
                            width: 24
                            height: 24
                            scale: Math.min(iconBody.width, iconBody.height) / 24
                            z: -1
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: iconBody.ink
                                strokeWidth: 1.8
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                PathSvg { path: board.iconPath(iconBody.iconName) }
                            }
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "postit"
                    Body {
                        id: postitBody
                        zoom: view.zoom
                        editingId: view.editingId
                        readonly property color paper: view.paperOf(fill)
                        labelColor: view.readable(paper)
                        // A soft shadow, as if it stood a little off the board.
                        Rectangle {
                            anchors.fill: parent
                            anchors.topMargin: 4
                            anchors.leftMargin: 2
                            anchors.rightMargin: -2
                            anchors.bottomMargin: -5
                            z: -2
                            color: Qt.alpha("black", theme.light ? 0.18 : 0.45)
                            radius: Math.min(2, theme.radius)
                        }
                        Rectangle {
                            anchors.fill: parent
                            z: -1
                            color: postitBody.paper
                            radius: Math.min(2, theme.radius)
                        }
                        // The gummed strip along the top.
                        Rectangle {
                            width: parent.width
                            height: Math.min(parent.height, 14)
                            z: -1
                            color: Qt.alpha("black", 0.08)
                            radius: Math.min(2, theme.radius)
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "text"
                    Item {
                        id: note
                        required property int itemId
                        required property real ix
                        required property real iy
                        required property string label
                        required property string fill
                        required property int fontSize
                        required property bool selected
                        required property real rot
                        x: ix
                        y: iy
                        z: 1
                        rotation: rot
                        width: noteText.implicitWidth
                        height: noteText.implicitHeight

                        // The board hit-tests text by the size it is drawn at.
                        function report() {
                            board.setMeasuredSize(itemId, Math.max(noteText.implicitWidth, 12), noteText.implicitHeight)
                        }
                        Text {
                            id: noteText
                            text: note.label
                            font.pixelSize: note.fontSize
                            color: view.inkOf(note.fill)
                            visible: view.editingId !== note.itemId
                            onImplicitWidthChanged: note.report()
                            onImplicitHeightChanged: note.report()
                            Component.onCompleted: note.report()
                        }
                        Loader {
                            active: note.selected
                            sourceComponent: Rectangle {
                                transformOrigin: Item.TopLeft
                                scale: 1 / view.zoom
                                x: -3 / view.zoom
                                y: -3 / view.zoom
                                width: Math.max(note.width, 12) * view.zoom + 6
                                height: note.height * view.zoom + 6
                                color: "transparent"
                                border.color: theme.accent
                                border.width: 2
                                radius: Math.min(4, theme.radius)
                            }
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "line"
                    Stroke { zoom: view.zoom; z: 1 }
                }

                DelegateChoice {
                    roleValue: "connector"
                    Stroke { zoom: view.zoom; arrow: true }
                }
            }
        }

        TextEdit {
            id: editor
            objectName: "editor"
            z: 10
            visible: view.editing
            readonly property bool note: view.editingKind === "text"
            readonly property bool triangle: view.editingKind === "triangle"
            readonly property real room: triangle ? view.editBox.width / 2 : view.editBox.width - 16
            x: note ? view.editBox.x : view.editBox.x + (view.editBox.width - room) / 2
            y: note ? view.editBox.y
                    : view.editBox.y + (view.editBox.height - height) / 2 + (triangle ? view.editBox.height / 6 : 0)
            // Text is as wide as its words; a label wraps inside its shape.
            width: note ? undefined : room
            horizontalAlignment: note ? TextEdit.AlignLeft : TextEdit.AlignHCenter
            wrapMode: note ? TextEdit.NoWrap : TextEdit.Wrap
            selectionColor: theme.accent
            selectedTextColor: theme.accentForeground
            selectByMouse: true
            // Turned with the shape it is written on.
            transform: Rotation {
                origin.x: view.editBox.x + view.editBox.width / 2 - editor.x
                origin.y: view.editBox.y + view.editBox.height / 2 - editor.y
                angle: view.editRotation
            }
            onTextChanged: if (view.editing) board.setText(view.editingId, text)
            onActiveFocusChanged: if (!activeFocus) view.commitEdit()
            Keys.onEscapePressed: view.commitEdit()
            // Enter finishes; Shift+Enter starts a new line.
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && !(event.modifiers & Qt.ShiftModifier)) {
                    event.accepted = true
                    view.commitEdit()
                }
            }
        }
    }

    // --- Guides, in screen coordinates -------------------------------------------

    Rectangle {
        visible: view.mode === "band" && view.moved
        x: view.bandRect.x
        y: view.bandRect.y
        width: view.bandRect.width
        height: view.bandRect.height
        color: Qt.alpha(theme.accent, 0.12)
        border.color: theme.accent
        border.width: 1
    }

    // How far the shape is turned, beside the pointer while it turns, and
    // lit when it has caught on an angle.
    Rectangle {
        objectName: "angle"
        visible: view.moved && (view.mode === "rotate" || view.mode === "endpoint"
                                || (view.mode === "draw" && view.tool === "line"))
        x: Math.min(view.pointer.x + 18, view.width - width - 8)
        y: Math.min(view.pointer.y + 18, view.height - height - 8)
        width: angleLabel.implicitWidth + 16
        height: angleLabel.implicitHeight + 8
        radius: Math.min(4, theme.radius)
        color: view.rotateCaught ? theme.accent : theme.background
        border.color: theme.accent
        Text {
            id: angleLabel
            objectName: "angleLabel"
            anchors.centerIn: parent
            color: view.rotateCaught ? theme.accentForeground : theme.foreground
            font.pixelSize: 13
            // Reads the language, so the number is written anew on a switch.
            text: translator.language, qsTr("%1°").arg(Math.round(view.shownAngle).toLocaleString(Qt.locale(), 'f', 0))
        }
    }

    // The box around several selected things, which sizes them together.
    Rectangle {
        objectName: "groupBox"
        visible: board.groupCount > 1 && view.tool === "select" && !view.editing
        x: Math.round(view.sx(board.groupBounds.x)) - 3
        y: Math.round(view.sy(board.groupBounds.y)) - 3
        width: board.groupBounds.width * view.zoom + 6
        height: board.groupBounds.height * view.zoom + 6
        color: "transparent"
        border.color: theme.accent
        border.width: 1
    }

    // What the dragged selection is lined up with.
    Repeater {
        model: view.guides
        delegate: Rectangle {
            required property var modelData
            readonly property bool upright: modelData.x1 === modelData.x2
            x: Math.round(view.sx(modelData.x1)) - (upright ? 0 : 6)
            y: Math.round(view.sy(modelData.y1)) - (upright ? 6 : 0)
            width: upright ? 1 : (modelData.x2 - modelData.x1) * view.zoom + 12
            height: upright ? (modelData.y2 - modelData.y1) * view.zoom + 12 : 1
            color: theme.accent
        }
    }

    Rectangle {
        visible: view.mode === "connect" && view.targetId !== 0
        x: view.sx(view.targetBox.x) - 5
        y: view.sy(view.targetBox.y) - 5
        width: view.targetBox.width * view.zoom + 10
        height: view.targetBox.height * view.zoom + 10
        rotation: view.targetRotation
        color: Qt.alpha(theme.accent, 0.1)
        border.color: theme.accent
        border.width: 2
        radius: Math.min(4, theme.radius)
    }

    Rectangle {
        id: preview
        visible: view.mode === "connect" && view.moved
        readonly property point from: {
            if (!visible)
                return Qt.point(0, 0)
            const w = view.toWorld(view.pointer)
            return view.toScreen(board.edgePoint(view.sourceId, w.x, w.y))
        }
        x: from.x
        y: from.y - 1
        width: Math.hypot(view.pointer.x - from.x, view.pointer.y - from.y)
        height: 2
        radius: 1
        color: theme.accent
        antialiasing: true
        transformOrigin: Item.Left
        rotation: Math.atan2(view.pointer.y - from.y, view.pointer.x - from.x) * 180 / Math.PI
    }

    Repeater {
        model: view.dotList
        delegate: Rectangle {
            required property var modelData
            readonly property bool near: Math.hypot(modelData.x - view.pointer.x, modelData.y - view.pointer.y) <= 11
            // A true circle, whatever the theme's corners.
            width: near ? 14 : 9
            height: width
            radius: width / 2
            x: modelData.x - width / 2
            y: modelData.y - height / 2
            color: near ? theme.accent : theme.background
            border.color: theme.accent
            border.width: 2
            Behavior on width { NumberAnimation { duration: 80 } }
        }
    }

    Repeater {
        model: view.handleList
        delegate: Rectangle {
            required property var modelData
            width: 10
            height: 10
            x: modelData.x - 5
            y: modelData.y - 5
            rotation: modelData.rot || 0
            // Round for the ends of a line.
            radius: modelData.end !== undefined ? 5 : Math.min(2, theme.radius)
            color: theme.background
            border.color: theme.accent
            border.width: 2
        }
    }

    MouseArea {
        id: input
        objectName: "input"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        preventStealing: true
        cursorShape: view.mode === "pan" ? Qt.ClosedHandCursor
                   : view.spaceHeld ? Qt.OpenHandCursor
                   : view.tool === "text" ? Qt.IBeamCursor
                   : view.tool !== "select" ? Qt.CrossCursor
                   : view.mode === "move" && view.moved ? Qt.SizeAllCursor
                   : view.mode === "resize" && view.activeHandle.edge
                     ? view.edgeCursor(view.activeHandle.edge, board.selectedRotation)
                   : view.mode === "rotate" ? Qt.ClosedHandCursor
                   : view.mode === "resize" || view.mode === "endpoint" || view.mode === "scale"
                     ? view.handleCursor(view.activeHandle)
                   : view.mode === "" && view.hoverHandle && view.altHeld && view.hoverHandle.end === undefined
                     && !view.hoverHandle.group
                     ? Qt.OpenHandCursor
                   : view.mode === "" && view.hoverHandle ? view.handleCursor(view.hoverHandle)
                   : view.mode === "" && view.hoverEdge ? view.edgeCursor(view.hoverEdge, view.hoverEdgeRotation)
                   : Qt.ArrowCursor

        onPressed: (mouse) => view.press(mouse)
        onPositionChanged: (mouse) => view.drag(mouse)
        onReleased: (mouse) => view.release(mouse)
        onDoubleClicked: (mouse) => view.doubleClick(mouse)
        onEntered: view.pointerInside = true
        onExited: {
            view.pointerInside = false
            if (view.mode === "") {
                view.hoverId = 0
                view.hoverEdge = ""
                view.hoverHandle = null
            }
        }

        // A mouse wheel zooms on the pointer; two fingers on a touchpad pan,
        // and zoom with Ctrl held.
        onWheel: (wheel) => {
            const at = Qt.point(wheel.x, wheel.y)
            const touchpad = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
            if (wheel.modifiers & Qt.ControlModifier) {
                if (touchpad)
                    view.zoomAt(at, Math.exp(wheel.pixelDelta.y * 0.008), false)
                else
                    view.zoomAt(at, Math.pow(1.25, wheel.angleDelta.y / 120), true)
            } else if (touchpad) {
                zoomAnim.stop()
                view.camX += wheel.pixelDelta.x
                view.camY += wheel.pixelDelta.y
            } else if (wheel.modifiers & Qt.ShiftModifier) {
                view.camX += (wheel.angleDelta.y || wheel.angleDelta.x) / 2
            } else if (wheel.angleDelta.y !== 0) {
                view.zoomAt(at, Math.pow(1.25, wheel.angleDelta.y / 120), true)
            } else {
                view.camX += wheel.angleDelta.x / 2
            }
        }
    }

    PinchHandler {
        target: null
        onScaleChanged: (delta) => view.zoomAt(centroid.position, delta, false)
    }

    // --- Delegates ---------------------------------------------------------------

    // What a rectangle, a circle, and a triangle share: a place, a label, and
    // the outline that shows they are selected.
    component Body: Item {
        id: body
        required property int itemId
        required property real ix
        required property real iy
        required property real iw
        required property real ih
        required property string label
        required property string fill
        required property bool selected
        required property real rot
        property real zoom: 1
        property int editingId: 0
        property color labelColor: theme.foreground
        property real labelOffset: 0
        property real labelWidth: width - 16
        // A colored shape is tinted with its color, over the background so it
        // hides what is behind it and its label reads in any theme.
        readonly property color ink: fill ? theme.colors[fill] : theme.foreground
        readonly property color paint: Qt.tint(theme.background, Qt.alpha(ink, fill ? 0.28 : 0.06))
        x: ix
        y: iy
        z: 1
        width: iw
        height: ih
        rotation: rot

        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: body.labelOffset
            width: Math.max(0, body.labelWidth)
            visible: body.editingId !== body.itemId
            text: body.label
            color: body.labelColor
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        // Drawn at the inverse of the zoom, so the outline is two pixels
        // wide however far in or out the canvas is.
        Loader {
            active: body.selected
            sourceComponent: Rectangle {
                transformOrigin: Item.TopLeft
                scale: 1 / body.zoom
                x: -3 / body.zoom
                y: -3 / body.zoom
                width: body.width * body.zoom + 6
                height: body.height * body.zoom + 6
                color: "transparent"
                border.color: theme.accent
                border.width: 2
                radius: Math.min(4, theme.radius)
            }
        }
    }

    // A line or a connector: one thin rectangle turned to run from end to
    // end, so following a moving shape is a rotation, not a redraw.
    component Stroke: Item {
        id: stroke
        required property int itemId
        required property real x1
        required property real y1
        required property real x2
        required property real y2
        required property string fill
        required property bool selected
        property real zoom: 1
        property bool arrow: false
        readonly property real length: Math.hypot(x2 - x1, y2 - y1)
        readonly property color ink: fill ? theme.colors[fill] : theme.foreground
        x: x1
        y: y1
        rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
        transformOrigin: Item.TopLeft

        Rectangle {
            visible: stroke.selected
            x: -3 / stroke.zoom
            width: stroke.length + 6 / stroke.zoom
            height: 2 + 8 / stroke.zoom
            y: -height / 2
            radius: height / 2
            color: Qt.alpha(theme.accent, 0.45)
        }
        Rectangle {
            width: Math.max(0, stroke.length - (stroke.arrow ? 6 : 0))
            height: 2
            y: -1
            radius: 1
            color: stroke.ink
            antialiasing: true
        }
        Shape {
            visible: stroke.arrow && stroke.length > 14
            x: stroke.length
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                fillColor: stroke.ink
                startX: 0
                startY: 0
                PathLine { x: -13; y: -6 }
                PathLine { x: -13; y: 6 }
                PathLine { x: 0; y: 0 }
            }
        }
    }
}
