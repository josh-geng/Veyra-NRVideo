// PS5 键鼠控制 dialog: keyboard & mouse → virtual DualSense for PS5 Remote Play.
// All state lives in the bridge (veyra.ps5Kbm / veyra.ps5KbmStats); this view only edits it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: kbmDialog
    objectName: "ps5-kbm-dialog"
    key: "ps5kbm"
    glyph: "gamepad"
    title: qsTr("PS5 键鼠控制")
    sub: qsTr("键盘鼠标 → 虚拟手柄。PS5 游戏只接收手柄摇杆，转向速度有上限；这不是原生鼠标瞄准。")
    dialogWidth: 720
    readonly property var kbm: veyra.ps5Kbm || ({})
    readonly property var st: veyra.ps5KbmStats || ({})
    readonly property var profile: kbm.profile || ({})
    readonly property var f: profile.fields || ({})
    readonly property bool on: kbm.enabled === true
    readonly property var cal: kbm.calibration || ({})
    actions: [{ label: qsTr("关闭") }, { label: qsTr("关闭并捕获键鼠"), primary: true }]
    onActionTriggered: label => {
        if (label === qsTr("关闭")) { dialogHost.close(); return }
        dialogHost.close()
        if (!veyra.ps5KbmCapture(true))
            veyra.notice(kbmDialog.on ? qsTr("需要正在进行的 PS5 串流或 PS5 手柄控制，并且 Veyra 窗口在前台") : qsTr("先打开「键鼠控制」开关"), false)
    }
    Connections {
        target: dialogHost
        function onDialogChanged() { if (dialogHost.dialog === "ps5kbm") veyra.ps5KbmLoad(); else if (kbmDialog.kbm.rebinding && kbmDialog.kbm.rebinding.index !== undefined) veyra.ps5KbmCancelRebind() }
    }
    function num(name, fallback) { const v = Number(f[name]); return isNaN(v) ? fallback : v }
    function fmt(v, d) { return Number(v).toFixed(d) }

    // A labelled slider bound to one profile field (names as in the profile file).
    component FieldSlider: VRow {
        id: fr
        property string field: ""
        property real from: 0
        property real to: 1
        property real step: 0
        property int decimals: 2
        property string unit: ""
        property real fallback: 0
        value: kbmDialog.fmt(kbmDialog.num(field, fallback), decimals) + unit
        VSlider {
            implicitWidth: 200
            valueFromModel: true
            enabledControl: kbmDialog.on
            from: fr.from; to: fr.to; stepSize: fr.step
            value: kbmDialog.num(fr.field, fr.fallback)
            defaultValue: fr.fallback
            onMoved: v => veyra.ps5KbmSet(fr.field, Number(v.toFixed(fr.decimals + 1)))
        }
    }
    component FieldSeg: VRow {
        id: sr
        property string field: ""
        property var options: []
        VSeg {
            enabled: kbmDialog.on
            options: sr.options
            current: String(kbmDialog.f[sr.field] || "")
            onPicked: id => veyra.ps5KbmSet(sr.field, id)
        }
    }
    component Meter: ColumnLayout {
        id: meter
        property string label: ""
        property real sx: 0
        property real sy: 0
        spacing: 4
        Rectangle {
            implicitWidth: 84; implicitHeight: 84; radius: 42
            color: Qt.rgba(1, 1, 1, 0.04); border.color: Theme.stroke2; border.width: 1
            Rectangle { anchors.centerIn: parent; width: 1; height: parent.height - 8; color: Theme.stroke }
            Rectangle { anchors.centerIn: parent; height: 1; width: parent.width - 8; color: Theme.stroke }
            Rectangle {
                width: 10; height: 10; radius: 5; color: Theme.accent
                x: parent.width / 2 - 5 + Math.max(-1, Math.min(1, meter.sx)) * 37
                y: parent.height / 2 - 5 + Math.max(-1, Math.min(1, meter.sy)) * 37
            }
        }
        Text { Layout.alignment: Qt.AlignHCenter; text: meter.label; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
    }

    // ------------------------------------------------------------------ status
    DialogSection { text: qsTr("状态") }
    DialogGroup {
        VRow {
            label: qsTr("键鼠控制")
            hint: kbmDialog.kbm.rawInput === false ? qsTr("原始输入不可用，见日志")
                 : kbmDialog.kbm.captured ? qsTr("已捕获 · Ctrl+Alt+Shift+Z 释放 · 切到其他窗口自动释放")
                 : kbmDialog.kbm.sessionActive ? qsTr("PS5 已连接；关闭面板并点击画面开始")
                 : qsTr("连接 PS5 串流或 PS5 手柄控制后可用；关闭时手柄输入与以前完全相同")
            VSwitch { objectName: "ps5-kbm-enabled"; checked: kbmDialog.on; onToggled: checked => veyra.ps5KbmSetEnabled(checked) }
        }
        VRow {
            label: qsTr("输入来源")
            hint: qsTr("混合：手柄与键鼠合成一个手柄状态，同一摇杆只取一边，不叠加")
            VSeg {
                objectName: "ps5-kbm-mode"
                enabled: kbmDialog.on
                options: [{ id: "2", label: qsTr("混合", "input mode") }, { id: "1", label: qsTr("仅键鼠") }, { id: "0", label: qsTr("仅手柄") }]
                current: String(kbmDialog.kbm.mode === undefined ? 2 : kbmDialog.kbm.mode)
                onPicked: id => veyra.ps5KbmSetMode(Number(id), kbmDialog.kbm.priority || 0)
            }
        }
        VRow {
            visible: kbmDialog.kbm.mode === 2
            label: qsTr("摇杆优先")
            hint: qsTr("较大者：手柄摇杆推得比键鼠多时用手柄（可随时拿起手柄驾驶）")
            VSeg {
                enabled: kbmDialog.on
                options: [{ id: "0", label: qsTr("较大者") }, { id: "1", label: qsTr("手柄") }, { id: "2", label: qsTr("键鼠") }]
                current: String(kbmDialog.kbm.priority || 0)
                onPicked: id => veyra.ps5KbmSetMode(kbmDialog.kbm.mode, Number(id))
            }
        }
    }

    // ------------------------------------------------------------------ profiles
    DialogSection { text: qsTr("方案") }
    DialogGroup {
        VRow {
            label: qsTr("当前方案")
            hint: qsTr("游戏中按 F1 步行 · F2 战斗 · F3 驾驶 · F4 菜单；切换时所有按键先松开")
            VSelect {
                objectName: "ps5-kbm-profile"
                implicitWidth: 200
                value: { const p = (kbmDialog.kbm.profiles || []).find(x => x.id === kbmDialog.kbm.active); return p ? p.label : "" }
                options: (kbmDialog.kbm.profiles || []).map(p => ({ id: String(p.id), label: p.label }))
                onPicked: id => veyra.ps5KbmSelectProfile(Number(id))
            }
        }
        VRow {
            label: qsTr("名称")
            RowLayout {
                spacing: 6
                DialogTextField { id: profileName; implicitWidth: 160; text: kbmDialog.profile.name || "" }
                VButton { text: qsTr("重命名"); ghost: true; onClicked: veyra.ps5KbmProfileAction("rename", profileName.text) }
            }
        }
        VRow {
            label: qsTr("管理")
            RowLayout {
                spacing: 6
                VButton { text: qsTr("新建"); ghost: true; onClicked: veyra.ps5KbmProfileAction("new") }
                VButton { text: qsTr("复制", "duplicate profile"); ghost: true; onClicked: veyra.ps5KbmProfileAction("duplicate") }
                VButton { text: qsTr("恢复默认"); ghost: true; onClicked: veyra.ps5KbmProfileAction("reset") }
                VButton { text: qsTr("删除"); ghost: true; enabled: (kbmDialog.kbm.profiles || []).length > 1; onClicked: veyra.ps5KbmProfileAction("delete") }
            }
        }
        VRow {
            label: qsTr("导入 / 导出")
            hint: qsTr("纯文本文件，不含任何 PSN 或配对信息")
            RowLayout {
                spacing: 6
                VButton { text: qsTr("导入"); ghost: true; onClicked: veyra.ps5KbmImport("") }
                VButton { text: qsTr("导出当前"); ghost: true; onClicked: veyra.ps5KbmExport("", false) }
                VButton { text: qsTr("导出全部"); ghost: true; onClicked: veyra.ps5KbmExport("", true) }
                VButton { text: qsTr("全部恢复默认"); ghost: true; onClicked: veyra.ps5KbmProfileAction("resetAll") }
            }
        }
    }

    // ------------------------------------------------------------------ mouse
    DialogSection { text: qsTr("鼠标 → 摇杆") }
    DialogGroup {
        FieldSeg { label: qsTr("鼠标驱动"); field: "mouse.target"; hint: qsTr("菜单方案用左摇杆当光标")
            options: [{ id: "right", label: qsTr("右摇杆") }, { id: "left", label: qsTr("左摇杆") }, { id: "none", label: qsTr("不用") }] }
        VRow {
            label: qsTr("鼠标 DPI")
            hint: qsTr("填鼠标实际 DPI，使灵敏度与硬件无关；不改变鼠标本身")
            DialogTextField { implicitWidth: 90; text: kbmDialog.f["mouse.dpi"] || "800"; onEdited: t => { if (Number(t) >= 50) veyra.ps5KbmSet("mouse.dpi", Number(t)) } }
        }
        FieldSlider { label: qsTr("灵敏度"); hint: qsTr("1.0：手移动约 5 英寸/秒达到游戏最大转速"); field: "mouse.sensitivity"; from: 0.1; to: 5; fallback: 1 }
        FieldSlider { label: qsTr("水平倍率"); field: "mouse.sensitivityX"; from: 0.2; to: 3; fallback: 1 }
        FieldSlider { label: qsTr("垂直倍率"); field: "mouse.sensitivityY"; from: 0.2; to: 3; fallback: 1 }
        FieldSlider { label: qsTr("瞄准（ADS）倍率"); hint: qsTr("按住 L2（右键或手柄）时生效"); field: "mouse.adsMultiplier"; from: 0.1; to: 2; fallback: 0.6 }
        FieldSlider { label: qsTr("精确瞄准倍率"); hint: qsTr("按住「精确」键时生效（战斗方案：鼠标侧键 5）"); field: "mouse.precisionMultiplier"; from: 0.1; to: 1; fallback: 0.4 }
        VRow {
            label: qsTr("反转 Y 轴")
            VSwitch { checked: kbmDialog.f["mouse.invertY"] === "1"; onToggled: checked => veyra.ps5KbmSet("mouse.invertY", checked) }
        }
        FieldSlider { label: qsTr("反死区"); hint: qsTr("游戏自己的摇杆死区；校准第 1 步可测得"); field: "mouse.antiDeadzone"; from: 0; to: 0.6; fallback: 0.12 }
        FieldSeg { label: qsTr("死区形状"); field: "mouse.deadzoneShape"; hint: qsTr("轴向：水平与垂直分别越过死区（适合轴向死区的游戏）")
            options: [{ id: "radial", label: qsTr("径向") }, { id: "axial", label: qsTr("轴向") }] }
        FieldSeg { label: qsTr("响应曲线"); field: "mouse.curve"; hint: qsTr("抵消游戏的摇杆曲线；校准表来自校准第 2 步")
            options: [{ id: "linear", label: qsTr("线性") }, { id: "power", label: qsTr("幂函数") }, { id: "table", label: qsTr("校准表") }] }
        FieldSlider { visible: kbmDialog.f["mouse.curve"] === "power"; label: qsTr("游戏曲线指数"); hint: qsTr("游戏转速 ≈ 推杆量^指数；常见 1.5–2.5"); field: "mouse.gamma"; from: 0.5; to: 3; fallback: 1 }
        FieldSlider { label: qsTr("最大推杆"); hint: qsTr("游戏转速到顶后继续推杆没有用；校准可测得饱和点"); field: "mouse.maxOutput"; from: 0.3; to: 1; fallback: 1 }
        FieldSlider { label: qsTr("平滑"); hint: qsTr("0 = 完全不平滑（默认）。增加会让停止变慢"); field: "mouse.smoothingMs"; from: 0; to: 30; step: 1; decimals: 0; unit: " ms"; fallback: 0 }
        FieldSlider { label: qsTr("鼠标加速"); hint: qsTr("0 = 关闭。可选，快速移动时放大"); field: "mouse.acceleration"; from: 0; to: 2; fallback: 0 }
        FieldSlider { label: qsTr("快速转身保留"); hint: qsTr("超过最大转速的移动在这段时间内继续转完；0 = 关闭，停止最干脆"); field: "mouse.overflowCarryMs"; from: 0; to: 200; step: 5; decimals: 0; unit: " ms"; fallback: 0 }
        FieldSeg { label: qsTr("对角"); field: "mouse.diagonal"; options: [{ id: "circular", label: qsTr("圆形") }, { id: "square", label: qsTr("方形") }] }
        FieldSlider { label: qsTr("轮盘灵敏度"); hint: qsTr("按住武器轮盘键时鼠标直接指向格子"); field: "mouse.radialGain"; from: 0.2; to: 4; fallback: 1 }
    }

    // ------------------------------------------------------------------ keyboard stick
    DialogSection { text: qsTr("键盘 → 左摇杆与扳机") }
    DialogGroup {
        FieldSlider { label: qsTr("推杆上升"); hint: qsTr("0 = 立即满推。驾驶方案用于渐进转向"); field: "keys.riseMs"; from: 0; to: 400; step: 5; decimals: 0; unit: " ms"; fallback: 0 }
        FieldSlider { label: qsTr("回中"); field: "keys.fallMs"; from: 0; to: 400; step: 5; decimals: 0; unit: " ms"; fallback: 0 }
        FieldSlider { label: qsTr("扳机上升"); hint: qsTr("键盘踩油门的渐进；松开总是立即归零"); field: "keys.triggerRiseMs"; from: 0; to: 400; step: 5; decimals: 0; unit: " ms"; fallback: 0 }
        FieldSeg { label: qsTr("同时按相反方向"); field: "keys.opposite"; options: [{ id: "last", label: qsTr("后按优先") }, { id: "neutral", label: qsTr("归中") }] }
        FieldSeg { label: qsTr("对角"); field: "keys.diagonal"; options: [{ id: "circular", label: qsTr("圆形") }, { id: "square", label: qsTr("方形") }] }
        FieldSlider { label: qsTr("滚轮按住时长"); hint: qsTr("每格滚轮按下一次；连续滚动最多排队 8 次"); field: "keys.wheelPulseMs"; from: 16; to: 200; step: 4; decimals: 0; unit: " ms"; fallback: 48 }
        VRow {
            label: qsTr("L2 时启用瞄准倍率")
            VSwitch { checked: kbmDialog.f["adsOnL2"] === "1"; onToggled: checked => veyra.ps5KbmSet("adsOnL2", checked) }
        }
    }

    // ------------------------------------------------------------------ bindings
    DialogSection { text: qsTr("按键") }
    DialogNote {
        text: qsTr("默认键位按 GTA V PlayStation 布局假设，并非 GTA VI 已核实的键位；请对照游戏内手柄设置修改。")
    }
    Repeater {
        model: (kbmDialog.profile.conflicts || []).filter(c => c.error)
        DialogNote { required property var modelData; text: qsTr("冲突：") + modelData.message }
    }
    Rectangle {
        // Rebinding capture area: keys come from the native filter, mouse buttons and the wheel from here.
        objectName: "ps5-kbm-capture-area"
        visible: kbmDialog.kbm.rebinding !== undefined && kbmDialog.kbm.rebinding.index !== undefined
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: 64
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.06)
        border.color: Theme.accent; border.width: 1
        Text {
            anchors.centerIn: parent
            text: qsTr("按下键盘按键，或在这里点击鼠标键 / 滚动滚轮")
            color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: mouse => veyra.ps5KbmRebindMouse(mouse.button, 0)
            onWheel: wheel => veyra.ps5KbmRebindMouse(0, Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y) ? (wheel.angleDelta.x > 0 ? 2 : -2) : (wheel.angleDelta.y > 0 ? 1 : -1))
        }
        VButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 8; text: qsTr("取消"); ghost: true; onClicked: veyra.ps5KbmCancelRebind() }
    }
    DialogGroup {
        Repeater {
            model: kbmDialog.profile.bindings || []
            VRow {
                id: br
                required property var modelData
                label: (modelData.modifier ? modelData.modifier + " + " : "") + modelData.key
                hint: modelData.radial ? qsTr("轮盘：按住时鼠标定位右摇杆") : ""
                RowLayout {
                    spacing: 4
                    VButton { text: qsTr("改键"); ghost: true; enabled: kbmDialog.on; onClicked: veyra.ps5KbmRebind(br.modelData.index, "key") }
                    VButton { text: br.modelData.modifier ? qsTr("去组合") : qsTr("+组合"); ghost: true; enabled: kbmDialog.on
                        onClicked: {
                            if (br.modelData.modifier) { const b = Object.assign({}, br.modelData); b.modifier = ""; veyra.ps5KbmSetBinding(b.index, b) }
                            else veyra.ps5KbmRebind(br.modelData.index, "modifier")
                        } }
                    VSelect {
                        implicitWidth: 130
                        value: br.modelData.target
                        options: (kbmDialog.kbm.targets || []).map(t => ({ id: t, label: t }))
                        onPicked: id => { const b = Object.assign({}, br.modelData); b.target = id; veyra.ps5KbmSetBinding(b.index, b) }
                    }
                    VSelect {
                        implicitWidth: 84
                        value: br.modelData.behavior === "toggle" ? qsTr("切换") : br.modelData.behavior === "repeat" ? qsTr("连发") : qsTr("按住")
                        options: [{ id: "hold", label: qsTr("按住") }, { id: "toggle", label: qsTr("切换") }, { id: "repeat", label: qsTr("连发") }]
                        onPicked: id => { const b = Object.assign({}, br.modelData); b.behavior = id; veyra.ps5KbmSetBinding(b.index, b) }
                    }
                    VButton { icon: true; iconName: "x"; ghost: true; tip: qsTr("删除"); onClicked: veyra.ps5KbmRemoveBinding(br.modelData.index) }
                }
            }
        }
        VRow {
            label: qsTr("添加按键")
            VButton { text: qsTr("添加"); ghost: true; enabled: kbmDialog.on; onClicked: veyra.ps5KbmAddBinding() }
        }
    }

    // ------------------------------------------------------------------ calibration
    DialogSection { text: qsTr("校准（手动引导）") }
    DialogNote {
        text: qsTr("自动从画面识别转速并不可靠，因此由你观察并标记。在游戏中找一处可自由转动视角的地方站定，关闭其他输入。标记可按空格，Esc 取消。")
    }
    DialogGroup {
        VRow {
            label: qsTr("1 · 死区")
            hint: kbmDialog.cal.phase === "deadzone" ? qsTr("右摇杆正在缓慢推向右侧…视角一开始转动就标记")
                 : kbmDialog.cal.deadzone !== undefined && kbmDialog.cal.deadzone !== null ? qsTr("测得死区 %1（已扣除约 220 ms 反应时间）").arg(Number(kbmDialog.cal.deadzone).toFixed(3))
                 : qsTr("Veyra 慢慢推动右摇杆，你在视角开始转动的瞬间标记")
            RowLayout {
                spacing: 6
                VButton { text: qsTr("开始"); ghost: true; enabled: kbmDialog.kbm.sessionActive === true && kbmDialog.cal.phase !== "deadzone"; onClicked: veyra.ps5KbmCalibration("deadzoneStart") }
                VButton { text: qsTr("开始转动了"); primary: true; visible: kbmDialog.cal.phase === "deadzone"; onClicked: veyra.ps5KbmCalibration("deadzoneMark") }
            }
        }
        VRow {
            id: turnRow
            label: qsTr("2 · 转速")
            property real deflection: 1.0
            hint: kbmDialog.cal.phase === "turn" ? qsTr("推杆 %1：同一参照物每次经过准星时标记，至少三次（两整圈）。已标记 %2 次").arg(Number(kbmDialog.cal.deflection).toFixed(2)).arg(kbmDialog.cal.marks)
                 : qsTr("依次测 0.3 … 1.0 几个推杆量；已测 %1 个").arg((kbmDialog.cal.samples || []).length)
            RowLayout {
                spacing: 6
                VSelect {
                    implicitWidth: 80
                    value: turnRow.deflection.toFixed(2)
                    options: [0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0].map(v => ({ id: String(v), label: v.toFixed(2) }))
                    onPicked: id => turnRow.deflection = Number(id)
                }
                VButton { text: qsTr("开始转"); ghost: true; visible: kbmDialog.cal.phase !== "turn"; enabled: kbmDialog.kbm.sessionActive === true; onClicked: veyra.ps5KbmCalibration("turnBegin", turnRow.deflection) }
                VButton { text: qsTr("标记"); primary: true; visible: kbmDialog.cal.phase === "turn"; onClicked: veyra.ps5KbmCalibration("turnMark") }
                VButton { text: qsTr("记录"); ghost: true; visible: kbmDialog.cal.phase === "turn"; onClicked: veyra.ps5KbmCalibration("turnKeep") }
            }
        }
        VRow {
            label: qsTr("结果")
            hint: {
                const r = kbmDialog.cal.report
                if (!r) return qsTr("测完死区和至少 3 个推杆量后生成")
                let t = qsTr("死区 %1 · 最大 %2°/s").arg(Number(r.deadzone).toFixed(3)).arg(Math.round(r.maxDegreesPerSecond))
                if (r.saturation !== undefined && r.saturation !== null) t += qsTr(" · 推杆 %1 已到顶").arg(Number(r.saturation).toFixed(2))
                if (r.timeRamp) t += qsTr(" · 游戏有随时间加速，无法消除")
                return t
            }
            RowLayout {
                spacing: 6
                VButton { text: qsTr("生成"); ghost: true; onClicked: veyra.ps5KbmCalibration("finish") }
                VButton { text: qsTr("应用到当前方案"); ghost: true; enabled: kbmDialog.cal.report !== undefined; onClicked: veyra.ps5KbmCalibration("apply") }
                VButton { text: qsTr("取消"); ghost: true; onClicked: veyra.ps5KbmCalibration("cancel") }
            }
        }
        VRow {
            label: qsTr("3 · 线性检查")
            hint: (kbmDialog.cal.result || "").length > 0 ? kbmDialog.cal.result
                 : qsTr("点按钮后关闭面板、点画面捕获，用鼠标慢慢（或快速）转正好一圈，再按 Ctrl+Alt+Shift+Z")
            RowLayout {
                spacing: 6
                VButton { text: qsTr("慢速一圈"); ghost: true; onClicked: veyra.ps5KbmCalibration("linearitySlow") }
                VButton { text: qsTr("快速一圈"); ghost: true; onClicked: veyra.ps5KbmCalibration("linearityFast") }
            }
        }
    }

    // ------------------------------------------------------------------ live
    DialogSection { text: qsTr("实时（本机测量，不是端到端延迟）") }
    DialogGroup {
        VRow {
            label: qsTr("输入")
            hint: qsTr("鼠标事件 %1/s（报告间隔约 %2 ms）· 按键事件 %3/s · 按住 %4")
                  .arg(Math.round(kbmDialog.st.mouseHz || 0)).arg(Number(kbmDialog.st.reportMs || 0).toFixed(2))
                  .arg(Math.round(kbmDialog.st.keyHz || 0)).arg(kbmDialog.st.held || 0)
        }
        VRow {
            label: qsTr("处理 / 发送")
            hint: qsTr("每次合成 %1 µs（最大 %2）· 发送间隔 %3 ms（最大 %4）· 超出最大转速 %5%")
                  .arg(Number(kbmDialog.st.processUs || 0).toFixed(1)).arg(Number(kbmDialog.st.processMaxUs || 0).toFixed(0))
                  .arg(Number(kbmDialog.st.submitMs || 0).toFixed(2)).arg(Number(kbmDialog.st.submitMaxMs || 0).toFixed(1))
                  .arg(Number(kbmDialog.st.saturated || 0).toFixed(0))
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 24
            Meter { label: qsTr("左摇杆"); sx: kbmDialog.st.leftX || 0; sy: kbmDialog.st.leftY || 0 }
            Meter { label: qsTr("右摇杆"); sx: kbmDialog.st.rightX || 0; sy: kbmDialog.st.rightY || 0 }
            ColumnLayout {
                spacing: 6
                Repeater {
                    model: [{ n: "L2", v: kbmDialog.st.l2 || 0 }, { n: "R2", v: kbmDialog.st.r2 || 0 }]
                    RowLayout {
                        required property var modelData
                        spacing: 6
                        Text { text: modelData.n; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                        Rectangle { implicitWidth: 120; implicitHeight: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.08)
                            Rectangle { width: parent.width * modelData.v; height: parent.height; radius: 3; color: Theme.accent } }
                    }
                }
                Text {
                    text: [kbmDialog.st.ads ? qsTr("瞄准") : "", kbmDialog.st.precision ? qsTr("精确") : "", kbmDialog.st.radial ? qsTr("轮盘") : "", kbmDialog.st.carrying ? qsTr("转身保留") : ""].filter(x => x).join(" · ")
                    color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 11
                }
            }
        }
    }
    DialogNote { visible: (kbmDialog.kbm.message || "").length > 0; text: kbmDialog.kbm.message || "" }
}
