import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0

// PS5 键鼠控制 dialog against a mock bridge: switch, mode, bindings list, rebinding area.
Item {
    id: fixture
    width: 900; height: 1600
    QtObject {
        id: backend
        property var ps5Kbm: ({available: true, enabled: false, captured: false, rawInput: true, sessionActive: true,
            mode: 2, priority: 0, active: 0, profiles: [{id: 0, label: "On foot"}, {id: 1, label: "Driving"}],
            targets: ["Cross", "Circle", "L2", "R2"],
            profile: {name: "On foot", fields: {"mouse.sensitivity": "1", "mouse.curve": "linear", "mouse.invertY": "0", "adsOnL2": "1"},
                bindings: [{index: 0, key: "W", modifier: "", target: "LeftUp", behavior: "hold", value: 1, repeatHz: 10, radial: false, label: "W -> LeftUp"},
                           {index: 1, key: "Tab", modifier: "", target: "L1", behavior: "hold", value: 1, repeatHz: 10, radial: true, label: "Tab -> L1 (radial)"}],
                conflicts: []},
            rebinding: ({}), calibration: {phase: "", samples: [], marks: 0, deflection: 0}, message: ""})
        property var ps5KbmStats: ({mouseHz: 1000, reportMs: 1, processUs: 0.5, submitMs: 4, leftX: 0, leftY: -1, rightX: 0.5, rightY: 0, l2: 0, r2: 1})
        property var enabledCalls: []
        property var mouseRebinds: []
        property var modeCalls: []
        function ps5KbmLoad() {}
        function ps5KbmSetEnabled(on) { enabledCalls = enabledCalls.concat([on]); const n = Object.assign({}, ps5Kbm); n.enabled = on; ps5Kbm = n }
        function ps5KbmSetMode(m, p) { modeCalls = modeCalls.concat([[m, p]]) }
        function ps5KbmRebindMouse(button, wheel) { mouseRebinds = mouseRebinds.concat([[button, wheel]]) }
        function ps5KbmCancelRebind() { const n = Object.assign({}, ps5Kbm); n.rebinding = ({}); ps5Kbm = n }
        function ps5KbmCapture(on) { return false }
        function ps5KbmSet(k, v) { return true }
        signal notice(string text, bool isError)
    }
    Item {
        id: host
        anchors.fill: parent
        property string dialog: "ps5kbm"
        property real motionScale: 1
        function close() { dialog = "" }
        function open(key) { dialog = key }
    }
    Ps5KbmDialog { id: panel; property var veyra: backend; dialogHost: host }
    TestCase {
        name: "Ps5Kbm"
        when: windowShown
        property bool oldMotion: false
        function initTestCase() { oldMotion = Theme.reduced; Theme.reduced = true; wait(60) }
        function cleanupTestCase() { Theme.reduced = oldMotion }
        function test_enable_switch_reaches_bridge() {
            const sw = findChild(panel, "ps5-kbm-enabled")
            verify(sw)
            mouseClick(sw)
            compare(backend.enabledCalls.length, 1)
            compare(backend.enabledCalls[0], true)
            compare(sw.checked, true)
        }
        function test_rebinding_area_only_while_rebinding() {
            const area = findChild(panel, "ps5-kbm-capture-area")
            verify(area)
            verify(!area.visible)
            const n = Object.assign({}, backend.ps5Kbm); n.rebinding = {index: 0, part: "key"}; backend.ps5Kbm = n
            tryVerify(() => area.visible)
            mouseClick(area, 20, area.height / 2, Qt.RightButton)
            compare(backend.mouseRebinds.length, 1)
            compare(backend.mouseRebinds[0][0], Qt.RightButton)
            backend.ps5KbmCancelRebind()
            tryVerify(() => !area.visible)
        }
    }
}
