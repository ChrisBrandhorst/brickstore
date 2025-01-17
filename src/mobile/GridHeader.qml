import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import BrickStore

Control {
    id: root

    // we expect to be inside a HeaderView.delegate for a Document

    property int visualIndex: index
    property int logicalIndex: TableView.view.model.logicalColumn(index)
    property Document document: TableView.view.model.document
    property int sortStatus: 0
    property int sortCount: 0

    readonly property real cellPadding: 2

    signal showMenu(logicalColumn: int, visualColumn: int)

    Connections {
        target: document.model
        function onSortColumnsChanged() { checkSortStatus() }
    }
    Component.onCompleted: checkSortStatus()

    function checkSortStatus() {
        let sorted = 0
        let sc = document.model.sortColumns
        for (let i = 0; i < sc.length; ++i) {
            if (sc[i].column === logicalIndex) {
                sorted = (i + 1) * (sc[i].order === Qt.AscendingOrder ? -1 : 1);
                break
            }
        }
        root.sortCount = sc.length
        root.sortStatus = sorted
    }

    FontMetrics { id: fm }

    // based on Qt's Material header
    implicitWidth: 20
    implicitHeight: fm.height * 1.5

    Loader {
        id: loaderText
        anchors.fill: parent
        asynchronous: true
        sourceComponent: Component {
            Text {
                id: headerText
                text: model.display // root.textRole ? model[root.textRole] : modelData
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: enabled ? root.Material.foreground : root.Material.hintTextColor
                fontSizeMode: Text.HorizontalFit
                minimumPointSize: font.pointSize / 4
                elide: Text.ElideMiddle
                clip: true
                padding: root.cellPadding
                font.bold: root.sortStatus === 1 || root.sortStatus === -1

                Rectangle {
                    id: rect
                    z: -1
                    anchors.fill: parent
                    color: root.Material.backgroundColor
                    property color gradientColor: root.Material.accentColor
                    property real d: sortCount && sortStatus ? (sortStatus < 0 ? -1 : 1) / sortCount : 0
                    property real e: (sortCount - Math.abs(sortStatus) + 1) * d
                    gradient: Gradient {
                        GradientStop { position: 0; color: sortStatus <= 0 ? rect.color : rect.gradientColor }
                        GradientStop { position: 0.5 + 0.4 * rect.e; color: rect.color }
                        GradientStop { position: 1; color: sortStatus >= 0 ? rect.color : rect.gradientColor }
                    }
                }
            }
        }
    }

    Rectangle {
        z: 1
        antialiasing: true
        height: 1 / Screen.devicePixelRatio
        color: Material.hintTextColor
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.right: parent.right
    }
    Rectangle {
        z: 1
        antialiasing: true
        width: 1 / Screen.devicePixelRatio
        color: Material.hintTextColor
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
    }

    TapHandler {
        onLongPressed: parent.showMenu(parent.logicalIndex, parent.visualIndex)
    }
}

//                DragHandler {
//                    id: columnMoveHandler
//                    onActiveChanged: {
//                        if (!active) {
//                            let pos = centroid.position
//                            console.log("drop at " + pos.x)
//                            pos = parent.mapToItem(header.contentItem, pos)
//                            console.log("drop recalc at " + pos.x)
//                            let drop = header.contentItem.childAt(pos.x, 4)
//                            console.log("drop on " + drop)
//                            console.log("drop on   vindex " + drop.visualIndex + " / lindex " + drop.logicalIndex)
//                            console.log("drag from vindex " + parent.visualIndex + " / lindex " + parent.logicalIndex)

//                            if (drop === parent) {
//                                console.log("drop on myself")
//                                return
//                            }

//                            if (drop && (parent.visualIndex !== drop.visualIndex)) {
//                                document.moveColumn(parent.logicalIndex, visualIndex, drop.visualIndex)
//                            }
//                        }
//                    }
//                }
