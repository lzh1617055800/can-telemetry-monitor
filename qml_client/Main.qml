import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: window
    width: 1180
    height: 760
    visible: true
    title: "CAN Telemetry Monitor"
    color: "#0b1220"

    property string apiBase: "http://127.0.0.1:8080"
    property int lastSequence: 0
    property real frameRate: 0
    property real busLoad: 0
    property int totalFrames: 0
    property string statusText: "正在连接 HTTP 服务…"
    property color statusColor: "#fbbf24"
    property var loadHistory: []

    function requestJson(path, callback) {
        var request = new XMLHttpRequest()
        request.open("GET", apiBase + path)
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE) {
                return
            }
            if (request.status < 200 || request.status >= 300) {
                statusText = "HTTP 服务不可用 (" + request.status + ")"
                statusColor = "#fda4af"
                return
            }
            try {
                callback(JSON.parse(request.responseText))
            } catch (error) {
                statusText = "JSON 响应解析失败"
                statusColor = "#fda4af"
            }
        }
        request.send()
    }

    function refresh() {
        requestJson("/api/can/stats", function(stats) {
            totalFrames = stats.total_received_frames
            frameRate = stats.receive_rate_fps
            busLoad = stats.estimated_bus_load_percent
            loadHistory = loadHistory.concat([
                Math.min(100, Math.max(0, busLoad))
            ]).slice(-60)
            loadCanvas.requestPaint()
            statusText = "CAN 在线 · REST + QML"
            statusColor = "#5eead4"
        })

        requestJson("/api/can/frames?after=" + lastSequence + "&limit=100",
                    function(payload) {
            for (var index = 0; index < payload.frames.length; ++index) {
                var frame = payload.frames[index]
                frameModel.append({
                    sequence: frame.sequence,
                    idHex: frame.id_hex,
                    dlc: frame.dlc,
                    dataText: frame.data.map(function(byteValue) {
                        return Number(byteValue).toString(16).padStart(2, "0")
                    }).join(" ")
                })
                lastSequence = Math.max(lastSequence, frame.sequence)
            }
            while (frameModel.count > 100) {
                frameModel.remove(0)
            }
        })
    }

    function sendFrame() {
        var values = dataField.text.trim().split(/\s+/).filter(function(value) {
            return value.length > 0
        }).map(function(value) {
            return Number(value)
        })
        var request = new XMLHttpRequest()
        request.open("POST", apiBase + "/api/can/send")
        request.setRequestHeader("Content-Type", "application/json")
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE) {
                return
            }
            sendResult.text = request.status >= 200 && request.status < 300
                ? "发送成功"
                : "发送失败: " + request.responseText
        }
        request.send(JSON.stringify({
            id: idField.text.trim(),
            extended: extendedField.checked,
            data: values
        }))
    }

    ListModel {
        id: frameModel
    }

    Timer {
        interval: 500
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: window.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: "C++17 · SocketCAN · Qt6/QML"
                    color: "#67e8f9"
                    font.pixelSize: 12
                    font.bold: true
                }
                Label {
                    text: "CAN Bus Monitor"
                    color: "#f8fafc"
                    font.pixelSize: 34
                    font.bold: true
                }
                Label {
                    text: "REST 远程数据 + QML 实时界面"
                    color: "#8ea2bd"
                }
            }

            Label {
                text: window.statusText
                color: window.statusColor
                padding: 10
                background: Rectangle {
                    radius: 14
                    color: "#16263a"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: 3

                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 86
                    radius: 12
                    color: "#111c2d"
                    border.color: "#23344d"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 6
                        Label {
                            text: index === 0
                                  ? "接收帧数"
                                  : index === 1
                                    ? "最近帧率"
                                    : "估算总线负载"
                            color: "#8ea2bd"
                        }
                        Label {
                            text: index === 0
                                  ? window.totalFrames
                                  : index === 1
                                    ? window.frameRate.toFixed(2) + " fps"
                                    : window.busLoad.toFixed(2) + "%"
                            color: "#f8fafc"
                            font.pixelSize: 24
                            font.bold: true
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 160
            radius: 12
            color: "#111c2d"
            border.color: "#23344d"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4

                Label { text: "总线负载曲线（最近 60 次采样）"; color: "#dbeafe" }
                Canvas {
                    id: loadCanvas
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onPaint: {
                        var context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.strokeStyle = "#23344d"
                        context.lineWidth = 1
                        context.beginPath()
                        context.moveTo(0, height - 1)
                        context.lineTo(width, height - 1)
                        context.stroke()
                        if (window.loadHistory.length < 2) {
                            return
                        }
                        context.strokeStyle = "#22d3ee"
                        context.lineWidth = 2
                        context.beginPath()
                        for (var index = 0; index < window.loadHistory.length; ++index) {
                            var x = index * width / 59
                            var y = height - window.loadHistory[index] * height / 100
                            if (index === 0) context.moveTo(x, y)
                            else context.lineTo(x, y)
                        }
                        context.stroke()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: "#111c2d"
            border.color: "#23344d"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "实时 CAN 帧表"; color: "#dbeafe"; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Label { text: "序号 / ID / DLC / 数据"; color: "#8ea2bd" }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: frameModel
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 34
                        color: index % 2 === 0 ? "#0d1727" : "#111c2d"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            Label { Layout.preferredWidth: 90; text: sequence; color: "#8ea2bd" }
                            Label { Layout.preferredWidth: 100; text: idHex; color: "#67e8f9" }
                            Label { Layout.preferredWidth: 70; text: dlc; color: "#dbeafe" }
                            Label { Layout.fillWidth: true; text: dataText; color: "#dbeafe"; font.family: "monospace" }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField { id: idField; Layout.preferredWidth: 130; text: "0x123"; placeholderText: "CAN ID" }
            TextField { id: dataField; Layout.fillWidth: true; text: "11 22 33 44"; placeholderText: "数据，例如 11 22 33 44" }
            CheckBox {
                id: extendedField
                text: "扩展帧"
                contentItem: Text {
                    text: extendedField.text
                    color: "#dbeafe"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: extendedField.indicator.width + extendedField.spacing
                }
            }
            Button { text: "发送 CAN 帧"; onClicked: window.sendFrame() }
            Label { id: sendResult; color: "#8ea2bd"; Layout.preferredWidth: 160; elide: Text.ElideRight }
        }
    }
}
