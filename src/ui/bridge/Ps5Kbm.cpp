// QML bridge: PS5 keyboard & mouse (键鼠控制) responsibilities.
//
// The engine (remoteplay::kbm::KbmEngine) is shared with every PS5 session; the session
// owner samples it right before each controller submission. This file owns only the
// user-facing parts: profiles and their persistence, capture on/off, rebinding, the
// guided calibration and the 250 ms statistics.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
#ifdef VEYRA_ENABLE_REMOTEPLAY
namespace {
namespace kbm = remoteplay::kbm;
std::filesystem::path kbmFile() { return remoteplay::profileDirectory() / L"keyboard-mouse.txt"; }
std::filesystem::path kbmSettings() { return remoteplay::profileDirectory() / L"settings.ini"; }
QString q(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
std::string u(const QString& s) { return s.toStdString(); }
QVariantMap bindingMap(const kbm::Binding& b, int index) {
    QVariantMap m;
    m["index"] = index;
    m["key"] = q(kbm::codeName(b.key));
    m["modifier"] = b.modifier ? q(kbm::codeName(b.modifier)) : QString();
    m["target"] = q(kbm::targetName(b.target));
    m["behavior"] = q(kbm::behaviorName(b.behavior));
    m["value"] = double(b.value);
    m["repeatHz"] = double(b.repeatHz);
    m["radial"] = b.radial;
    m["label"] = q(kbm::bindingLabel(b));
    return m;
}
QVariantList targetList() {
    QVariantList out;
    for (int t = 1; t < int(kbm::Target::Count); ++t) out << q(kbm::targetName(kbm::Target(t)));
    return out;
}
} // namespace

// A PS5 session that accepts controller input: a stream that is not view-only, or PS5 手柄控制.
bool QmlPlayerBridge::ps5KbmSessionActive() const {
    const auto& i = *impl_;
    const auto& s = i.snapshot;
    const bool stream = s.remotePlay && (s.running || s.transport == engine::TransportState::Opening);
    return (stream && !i.ps5Form.value("viewOnly").toBool()) || i.engine.remoteControlRequested();
}

void QmlPlayerBridge::setupPs5Kbm() {
    auto& i = *impl_;
    if (i.kbmCapture) return;
    i.kbmCapture = std::make_unique<Ps5KbmCapture>(i.kbm);
    connect(i.kbmCapture.get(), &Ps5KbmCapture::capturedChanged, this, [this](bool on) {
        auto& j = *impl_;
        // Linearity test: counts between capture and release are one revolution.
        if (j.kbmCal.phase.startsWith(QStringLiteral("linearity"))) {
            const auto counts = j.kbm->stats().mouseCountsX;
            if (on) j.kbmCal.countsAtCapture = counts;
            else {
                const auto travelled = counts - j.kbmCal.countsAtCapture;
                if (j.kbmCal.phase == QStringLiteral("linearitySlow")) j.kbmCal.slowCounts = travelled; else j.kbmCal.fastCounts = travelled;
                if (j.kbmCal.slowCounts && j.kbmCal.fastCounts) {
                    const auto v = kbm::linearity(j.kbmCal.slowCounts, j.kbmCal.fastCounts);
                    j.kbmCal.result = tr("慢/快一圈计数比 %1：%2").arg(double(v.ratio), 0, 'f', 2).arg(q(v.message));
                }
                j.kbmCal.phase.clear();
            }
        }
        emit ps5KbmChanged();
    });
    connect(i.kbmCapture.get(), &Ps5KbmCapture::releaseRequested, this, [this] {
        impl_->kbmWantCapture = false;
        emit notice(tr("已释放键盘和鼠标，PS5 虚拟手柄已归零（点击画面重新捕获）"), false);
    });
    connect(i.kbmCapture.get(), &Ps5KbmCapture::focusLost, this, [this] {
        // Coming back to the window does not re-capture by itself: the user clicks the picture.
        impl_->kbmWantCapture = false;
        emit ps5KbmChanged();
    });
    connect(i.kbmCapture.get(), &Ps5KbmCapture::deviceRemoved, this, [this] {
        impl_->kbmWantCapture = false;
        emit notice(tr("键盘或鼠标已断开，所有模拟按键已松开；重新连接后点击画面继续"), true);
    });
}

void QmlPlayerBridge::ps5KbmLoad() {
    auto& i = *impl_;
    setupPs5Kbm();
    if (i.kbmLoaded) { emit ps5KbmChanged(); return; }
    i.kbmLoaded = true;
    i.kbmEnabled = GetPrivateProfileIntW(L"KeyboardMouse", L"Enabled", 0, kbmSettings().c_str()) != 0;
    std::error_code ec;
    if (std::filesystem::exists(kbmFile(), ec)) {
        QFile file(QString::fromStdWString(kbmFile().wstring()));
        std::optional<kbm::ProfileSet> loaded;
        QString why;
        if (file.size() > 512 * 1024) why = QStringLiteral("file too large");
        else if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            file.close();
            auto r = kbm::parse(std::string_view(bytes.constData(), size_t(bytes.size())));
            for (const auto& w : r.warnings) veyra::log::warn("ps5-kbm", "profile file: " + w);
            if (r.set) loaded = std::move(r.set); else why = q(r.error);
        } else why = file.errorString();
        if (loaded) i.kbm->setProfiles(std::move(*loaded));
        else {
            // Keep the unreadable file for the user; never overwrite it silently.
            const auto stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdWString();
            auto backup = kbmFile(); backup.replace_filename(L"keyboard-mouse.corrupt-" + stamp + L".txt");
            std::filesystem::rename(kbmFile(), backup, ec);
            veyra::log::error("ps5-kbm", "profile file unreadable (" + u(why) + "); defaults restored, original kept as " + backup.filename().string());
            i.kbmMessage = tr("键鼠配置无法读取，已恢复默认；原文件已保留为 %1").arg(QString::fromStdWString(backup.filename().wstring()));
            i.kbm->setProfiles(kbm::defaultProfiles());
        }
    }
    emit ps5KbmChanged();
}

void QmlPlayerBridge::savePs5Kbm() {
    auto& i = *impl_;
    if (!i.kbmSaveTimer) {
        i.kbmSaveTimer = new QTimer(this);
        i.kbmSaveTimer->setSingleShot(true);
        connect(i.kbmSaveTimer, &QTimer::timeout, this, [this] {
            const auto text = kbm::serialize(impl_->kbm->profiles());
            std::error_code ec;
            std::filesystem::create_directories(kbmFile().parent_path(), ec);
            QSaveFile file(QString::fromStdWString(kbmFile().wstring())); // atomic replace: no half-written profile
            if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), qint64(text.size())) != qint64(text.size()) || !file.commit()) {
                veyra::log::error("ps5-kbm", "saving profiles failed: " + u(file.errorString()));
                impl_->kbmMessage = tr("键鼠配置保存失败，请检查用户目录权限");
                emit ps5KbmChanged();
            }
        });
    }
    i.kbmSaveTimer->start(400);
}

QVariantMap QmlPlayerBridge::ps5Kbm() const {
    const auto& i = *impl_;
    QVariantMap out;
    out["available"] = true;
    out["enabled"] = i.kbmEnabled;
    out["captured"] = i.kbmCapture && i.kbmCapture->captured();
    out["rawInput"] = i.kbmCapture && i.kbmCapture->rawInputAvailable();
    out["sessionActive"] = ps5KbmSessionActive();
    const auto set = i.kbm->profiles();
    out["mode"] = int(set.mode);
    out["priority"] = int(set.priority);
    out["active"] = set.active;
    QVariantList profiles;
    for (std::size_t p = 0; p < set.profiles.size(); ++p) profiles << QVariantMap{{"id", int(p)}, {"label", q(set.profiles[p].name)}};
    out["profiles"] = profiles;
    const auto& p = set.profiles[std::size_t(set.active)];
    QVariantMap profile, fields;
    profile["name"] = q(p.name);
    for (const auto& [k, v] : kbm::profileFields(p)) fields[q(k)] = q(v);
    profile["fields"] = fields;
    QVariantList bindings, conflicts;
    for (std::size_t b = 0; b < p.bindings.size(); ++b) bindings << bindingMap(p.bindings[b], int(b));
    for (const auto& c : kbm::findConflicts(p)) conflicts << QVariantMap{{"first", int(c.first)}, {"second", int(c.second)}, {"error", c.error}, {"message", q(c.message)}};
    profile["bindings"] = bindings;
    profile["conflicts"] = conflicts;
    out["profile"] = profile;
    out["targets"] = targetList();
    out["rebinding"] = i.kbmRebindIndex >= 0 ? QVariantMap{{"index", i.kbmRebindIndex}, {"part", i.kbmRebindPart}} : QVariantMap{};
    QVariantMap cal;
    cal["phase"] = i.kbmCal.phase;
    cal["deadzone"] = i.kbmCal.haveDeadzone ? QVariant(double(i.kbmCal.deadzone)) : QVariant();
    cal["deflection"] = double(i.kbmCal.probe.deflection());
    cal["marks"] = i.kbmCal.probe.marks();
    QVariantList samples;
    for (const auto& s : i.kbmCal.samples) samples << QVariantMap{{"deflection", double(s.deflection)}, {"seconds", double(s.secondsPerRevolution)}, {"first", double(s.firstRevolutionSeconds)}};
    cal["samples"] = samples;
    if (i.kbmCal.report) {
        QStringList notes;
        for (const auto& n : i.kbmCal.report->notes) notes << q(n);
        cal["report"] = QVariantMap{{"deadzone", double(i.kbmCal.report->deadzone)},
            {"saturation", i.kbmCal.report->saturation ? QVariant(double(*i.kbmCal.report->saturation)) : QVariant()},
            {"timeRamp", i.kbmCal.report->timeRamp}, {"maxDegreesPerSecond", double(i.kbmCal.report->maxDegreesPerSecond)},
            {"table", i.kbmCal.report->curve.kind == kbm::CurveKind::Table}, {"notes", notes}};
    }
    cal["result"] = i.kbmCal.result;
    out["calibration"] = cal;
    out["message"] = i.kbmMessage;
    return out;
}

QVariantMap QmlPlayerBridge::ps5KbmStats() const {
    const auto s = impl_->kbm->stats();
    QVariantMap m;
    m["enabled"] = s.enabled;
    m["profile"] = q(s.profileName);
    m["mouseHz"] = s.mouseEventsPerSecond;
    m["keyHz"] = s.keyEventsPerSecond;
    m["reportMs"] = s.mouseReportIntervalMs;
    m["processUs"] = s.composeAverageUs;
    m["processMaxUs"] = s.composeMaxUs;
    m["submitMs"] = s.submitIntervalAverageMs;
    m["submitMaxMs"] = s.submitIntervalMaxMs;
    m["saturated"] = s.saturatedPercent;
    m["leftX"] = s.leftX; m["leftY"] = s.leftY; m["rightX"] = s.rightX; m["rightY"] = s.rightY;
    m["l2"] = s.l2; m["r2"] = s.r2;
    m["buttons"] = qulonglong(s.buttons);
    m["held"] = s.heldInputs;
    m["radial"] = s.radial; m["ads"] = s.ads; m["precision"] = s.precision; m["carrying"] = s.carrying;
    return m;
}

void QmlPlayerBridge::ps5KbmSetEnabled(bool on) {
    auto& i = *impl_;
    ps5KbmLoad();
    i.kbmEnabled = on;
    WritePrivateProfileStringW(L"KeyboardMouse", L"Enabled", on ? L"1" : L"0", kbmSettings().c_str());
    if (!on) { i.kbmWantCapture = false; if (i.kbmCapture) i.kbmCapture->capture(false); }
    veyra::log::info("ps5-kbm", std::string("feature ") + (on ? "enabled" : "disabled"));
    emit ps5KbmChanged();
}

bool QmlPlayerBridge::ps5KbmCapture(bool on) {
    auto& i = *impl_;
    ps5KbmLoad();
    if (!on) { i.kbmWantCapture = false; if (i.kbmCapture) i.kbmCapture->capture(false); return true; }
    if (!i.kbmEnabled || !ps5KbmSessionActive() || !i.videoWindow) return false;
    if (i.kbm->profiles().mode == kbm::InputMode::ControllerOnly) {
        emit notice(tr("输入模式为「仅手柄」，键鼠不会发送到 PS5；在键鼠控制里切换模式"), false);
        return false;
    }
    i.kbmCapture->setWindow(GetAncestor(i.videoWindow, GA_ROOT));
    const bool ok = i.kbmCapture->capture(true);
    i.kbmWantCapture = !ok; // the tick retries once the window is in the foreground
    if (ok) emit notice(tr("键鼠已接管 PS5 虚拟手柄 · Ctrl+Alt+Shift+Z 释放 · F1–F4 切换方案"), false);
    return ok;
}

void QmlPlayerBridge::ps5KbmSetMode(int mode, int priority) {
    auto& i = *impl_;
    ps5KbmLoad();
    i.kbm->setMode(kbm::InputMode(std::clamp(mode, 0, 2)), kbm::StickPriority(std::clamp(priority, 0, 2)));
    if (mode == 0 && i.kbmCapture) i.kbmCapture->capture(false);
    savePs5Kbm();
    emit ps5KbmChanged();
}

void QmlPlayerBridge::ps5KbmSelectProfile(int index) {
    ps5KbmLoad();
    impl_->kbm->selectProfile(index);
    savePs5Kbm();
    emit ps5KbmChanged();
}

bool QmlPlayerBridge::ps5KbmSet(const QString& field, const QVariant& value) {
    auto& i = *impl_;
    ps5KbmLoad();
    auto set = i.kbm->profiles();
    auto& p = set.profiles[std::size_t(set.active)];
    QString text = value.typeId() == QMetaType::Bool ? (value.toBool() ? QStringLiteral("1") : QStringLiteral("0")) : value.toString();
    std::string why;
    if (!kbm::setProfileField(p, u(field), u(text), &why)) {
        i.kbmMessage = tr("无法设置 %1：%2").arg(field, q(why));
        emit ps5KbmChanged();
        return false;
    }
    i.kbmMessage.clear();
    i.kbm->setProfiles(std::move(set)); // releases held inputs: settings never change mid-press
    savePs5Kbm();
    emit ps5KbmChanged();
    return true;
}

bool QmlPlayerBridge::ps5KbmSetBinding(int index, const QVariantMap& binding) {
    auto& i = *impl_;
    ps5KbmLoad();
    auto set = i.kbm->profiles();
    auto& p = set.profiles[std::size_t(set.active)];
    if (index < 0 || index >= int(p.bindings.size())) return false;
    QString text = binding.value("modifier").toString().isEmpty() ? binding.value("key").toString()
                                                                    : binding.value("modifier").toString() + "+" + binding.value("key").toString();
    text += ">" + binding.value("target").toString();
    const auto behavior = binding.value("behavior", QStringLiteral("hold")).toString();
    if (behavior != QStringLiteral("hold")) text += ":" + behavior;
    if (binding.contains("value")) text += ":v=" + QString::number(binding.value("value").toDouble());
    if (behavior == QStringLiteral("repeat")) text += ":hz=" + QString::number(binding.value("repeatHz", 10).toDouble());
    if (binding.value("radial").toBool()) text += ":radial";
    std::string why;
    auto parsed = kbm::parseBindingText(u(text), &why);
    if (!parsed) { i.kbmMessage = tr("按键设置无效：%1").arg(q(why)); emit ps5KbmChanged(); return false; }
    p.bindings[std::size_t(index)] = *parsed;
    (void)kbm::sanitize(p);
    i.kbmMessage.clear();
    i.kbm->setProfiles(std::move(set));
    savePs5Kbm();
    emit ps5KbmChanged();
    return true;
}

void QmlPlayerBridge::ps5KbmAddBinding() {
    auto& i = *impl_;
    ps5KbmLoad();
    auto set = i.kbm->profiles();
    auto& p = set.profiles[std::size_t(set.active)];
    if (p.bindings.size() >= kbm::MaxBindings) return;
    kbm::Binding b; b.key = kbm::code::G; b.target = kbm::Target::Cross; // placeholder; the row opens for rebinding
    p.bindings.push_back(b);
    const int index = int(p.bindings.size()) - 1;
    i.kbm->setProfiles(std::move(set));
    savePs5Kbm();
    ps5KbmRebind(index, QStringLiteral("key"));
}

void QmlPlayerBridge::ps5KbmRemoveBinding(int index) {
    auto& i = *impl_;
    ps5KbmLoad();
    auto set = i.kbm->profiles();
    auto& p = set.profiles[std::size_t(set.active)];
    if (index < 0 || index >= int(p.bindings.size())) return;
    p.bindings.erase(p.bindings.begin() + index);
    i.kbm->setProfiles(std::move(set));
    savePs5Kbm();
    emit ps5KbmChanged();
}

void QmlPlayerBridge::ps5KbmRebind(int index, const QString& part) {
    auto& i = *impl_;
    ps5KbmLoad();
    if (!i.kbmCapture || i.kbmCapture->captured()) return;
    if (!i.videoWindow) return;
    i.kbmRebindIndex = index; i.kbmRebindPart = part;
    i.kbmCapture->setWindow(GetAncestor(i.videoWindow, GA_ROOT));
    i.kbmCapture->armRebind([this](std::uint16_t vk) {
        // Delivered on the UI thread (native event filter); finish asynchronously.
        QMetaObject::invokeMethod(this, [this, vk] {
            auto& j = *impl_;
            const int index = j.kbmRebindIndex; const QString part = j.kbmRebindPart;
            j.kbmRebindIndex = -1;
            if (index < 0) return;
            auto set = j.kbm->profiles();
            auto& p = set.profiles[std::size_t(set.active)];
            if (index >= int(p.bindings.size()) || !kbm::validCode(vk)) { emit ps5KbmChanged(); return; }
            if (part == QStringLiteral("modifier")) p.bindings[std::size_t(index)].modifier = vk == p.bindings[std::size_t(index)].key ? kbm::code::None : vk;
            else p.bindings[std::size_t(index)].key = vk;
            (void)kbm::sanitize(p);
            j.kbm->setProfiles(std::move(set));
            savePs5Kbm();
            emit ps5KbmChanged();
        }, Qt::QueuedConnection);
    });
    emit ps5KbmChanged();
}

void QmlPlayerBridge::ps5KbmRebindMouse(int qtButton, int wheel) {
    auto& i = *impl_;
    const int index = i.kbmRebindIndex;
    if (index < 0) return;
    kbm::InputCode c = kbm::code::None;
    switch (wheel) { case 1: c = kbm::code::WheelUp; break; case -1: c = kbm::code::WheelDown; break; case 2: c = kbm::code::WheelRight; break; case -2: c = kbm::code::WheelLeft; break; default: break; }
    if (!c) {
        switch (qtButton) {
        case Qt::LeftButton: c = kbm::code::MouseLeft; break;
        case Qt::RightButton: c = kbm::code::MouseRight; break;
        case Qt::MiddleButton: c = kbm::code::MouseMiddle; break;
        case Qt::BackButton: c = kbm::code::MouseX1; break;
        case Qt::ForwardButton: c = kbm::code::MouseX2; break;
        default: return;
        }
    }
    const QString part = i.kbmRebindPart;
    ps5KbmCancelRebind();
    auto set = i.kbm->profiles();
    auto& p = set.profiles[std::size_t(set.active)];
    if (index >= int(p.bindings.size())) return;
    if (part == QStringLiteral("modifier")) {
        if (kbm::isWheel(c)) { i.kbmMessage = tr("滚轮不能作为组合键"); emit ps5KbmChanged(); return; }
        p.bindings[std::size_t(index)].modifier = c;
    } else p.bindings[std::size_t(index)].key = c;
    (void)kbm::sanitize(p);
    i.kbm->setProfiles(std::move(set));
    savePs5Kbm();
    emit ps5KbmChanged();
}

void QmlPlayerBridge::ps5KbmCancelRebind() {
    auto& i = *impl_;
    i.kbmRebindIndex = -1;
    if (i.kbmCapture) i.kbmCapture->cancelRebind();
    emit ps5KbmChanged();
}

void QmlPlayerBridge::ps5KbmProfileAction(const QString& action, const QString& arg) {
    auto& i = *impl_;
    ps5KbmLoad();
    auto set = i.kbm->profiles();
    const auto active = std::size_t(set.active);
    if (action == QStringLiteral("resetAll")) {
        const auto mode = set.mode; const auto priority = set.priority;
        set = kbm::defaultProfiles(); set.mode = mode; set.priority = priority;
    } else if (action == QStringLiteral("reset")) {
        // Back to the shipped profile of the same name, or On foot for a custom one.
        const auto defaults = kbm::defaultProfiles();
        auto it = std::find_if(defaults.profiles.begin(), defaults.profiles.end(), [&](const auto& p) { return p.name == set.profiles[active].name; });
        const auto name = set.profiles[active].name;
        set.profiles[active] = it != defaults.profiles.end() ? *it : kbm::defaultOnFoot();
        set.profiles[active].name = name;
    } else if (action == QStringLiteral("new") || action == QStringLiteral("duplicate")) {
        if (set.profiles.size() >= kbm::MaxProfiles) { i.kbmMessage = tr("最多 %1 个方案").arg(int(kbm::MaxProfiles)); emit ps5KbmChanged(); return; }
        auto copy = action == QStringLiteral("new") ? kbm::defaultOnFoot() : set.profiles[active];
        copy.name = (action == QStringLiteral("new") ? std::string("Custom") : copy.name + " copy").substr(0, kbm::MaxNameLength);
        set.profiles.push_back(std::move(copy));
        set.active = int(set.profiles.size()) - 1;
    } else if (action == QStringLiteral("rename")) {
        const auto name = u(arg.trimmed()).substr(0, kbm::MaxNameLength);
        if (name.empty()) return;
        set.profiles[active].name = name;
    } else if (action == QStringLiteral("delete")) {
        if (set.profiles.size() <= 1) return;
        set.profiles.erase(set.profiles.begin() + std::ptrdiff_t(active));
        set.active = std::min<int>(set.active, int(set.profiles.size()) - 1);
    } else return;
    i.kbmMessage.clear();
    i.kbm->setProfiles(std::move(set));
    savePs5Kbm();
    emit ps5KbmChanged();
}

bool QmlPlayerBridge::ps5KbmExport(const QUrl& url, bool allProfiles) {
    auto& i = *impl_;
    ps5KbmLoad();
    const auto set = i.kbm->profiles();
    const auto text = allProfiles ? kbm::serialize(set) : kbm::serializeProfile(set.profiles[std::size_t(set.active)]);
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) path = QFileDialog::getSaveFileName(nullptr, tr("导出键鼠方案"), allProfiles ? QStringLiteral("veyra-ps5-keyboard-mouse.txt") : q(set.profiles[std::size_t(set.active)].name) + QStringLiteral(".txt"), tr("键鼠方案 (*.txt)"));
    if (path.isEmpty()) return false;
    QSaveFile file(path);
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(text.data(), qint64(text.size())) == qint64(text.size()) && file.commit();
    i.kbmMessage = ok ? tr("已导出键鼠方案") : tr("导出失败：%1").arg(file.errorString());
    emit ps5KbmChanged();
    return ok;
}

bool QmlPlayerBridge::ps5KbmImport(const QUrl& url) {
    auto& i = *impl_;
    ps5KbmLoad();
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) path = QFileDialog::getOpenFileName(nullptr, tr("导入键鼠方案"), QString(), tr("键鼠方案 (*.txt)"));
    if (path.isEmpty()) return false;
    QFile file(path);
    if (file.size() > 512 * 1024 || !file.open(QIODevice::ReadOnly)) { i.kbmMessage = tr("无法读取所选文件"); emit ps5KbmChanged(); return false; }
    const QByteArray bytes = file.readAll();
    auto r = kbm::parse(std::string_view(bytes.constData(), size_t(bytes.size())));
    if (!r.set) { i.kbmMessage = tr("不是有效的键鼠方案：%1").arg(q(r.error)); emit ps5KbmChanged(); return false; }
    auto set = i.kbm->profiles();
    // Imported profiles are appended; existing ones are never replaced by an import.
    int added = 0;
    for (auto& p : r.set->profiles) {
        if (set.profiles.size() >= kbm::MaxProfiles) break;
        set.profiles.push_back(std::move(p)); ++added;
    }
    if (added) set.active = int(set.profiles.size()) - added;
    i.kbm->setProfiles(std::move(set));
    savePs5Kbm();
    i.kbmMessage = tr("已导入 %1 个方案%2").arg(added).arg(r.warnings.empty() ? QString() : tr("（%1 条已修正，见日志）").arg(int(r.warnings.size())));
    for (const auto& w : r.warnings) veyra::log::warn("ps5-kbm", "import: " + w);
    emit ps5KbmChanged();
    return added > 0;
}

void QmlPlayerBridge::ps5KbmCalibration(const QString& action, double value) {
    auto& i = *impl_;
    ps5KbmLoad();
    auto& c = i.kbmCal;
    const auto now = remoteplay::monotonic100ns();
    if (action == QStringLiteral("cancel")) {
        i.kbm->setRightStickOverride(std::nullopt);
        if (i.kbmCapture) i.kbmCapture->cancelRebind();
        c = {};
    } else if (action == QStringLiteral("deadzoneStart")) {
        if (!ps5KbmSessionActive()) { i.kbmMessage = tr("先连接 PS5（串流或手柄控制），在游戏里站定后再校准"); emit ps5KbmChanged(); return; }
        if (i.kbmCapture) i.kbmCapture->capture(false);
        c.phase = QStringLiteral("deadzone"); c.sweep.start(now);
    } else if (action == QStringLiteral("deadzoneMark") && c.phase == QStringLiteral("deadzone")) {
        c.deadzone = c.sweep.mark(now); c.haveDeadzone = true; c.phase.clear();
        i.kbm->setRightStickOverride(std::nullopt);
    } else if (action == QStringLiteral("turnBegin")) {
        if (!ps5KbmSessionActive()) return;
        if (i.kbmCapture) i.kbmCapture->capture(false);
        c.phase = QStringLiteral("turn"); c.probe.begin(float(value), now);
        i.kbm->setRightStickOverride(std::array<float, 2>{float(value), 0.f});
    } else if (action == QStringLiteral("turnMark") && c.phase == QStringLiteral("turn")) {
        c.probe.mark(now);
    } else if (action == QStringLiteral("turnKeep") && c.phase == QStringLiteral("turn")) {
        if (auto s = c.probe.result()) {
            std::erase_if(c.samples, [&](const auto& o) { return std::abs(o.deflection - s->deflection) < 1e-3f; });
            c.samples.push_back(*s);
        } else i.kbmMessage = tr("需要至少标记三次（两整圈）");
        c.phase.clear();
        i.kbm->setRightStickOverride(std::nullopt);
    } else if (action == QStringLiteral("finish")) {
        c.report = kbm::buildCalibration(c.haveDeadzone ? c.deadzone : i.kbm->profiles().profiles[std::size_t(i.kbm->profiles().active)].mouse.antiDeadzone, c.samples);
        i.kbm->setRightStickOverride(std::nullopt);
        c.phase.clear();
    } else if (action == QStringLiteral("apply") && c.report) {
        auto set = i.kbm->profiles();
        kbm::applyCalibration(set.profiles[std::size_t(set.active)], *c.report);
        i.kbm->setProfiles(std::move(set));
        savePs5Kbm();
        c.result = tr("已应用到当前方案");
    } else if (action == QStringLiteral("linearitySlow") || action == QStringLiteral("linearityFast")) {
        c.phase = action;
        if (action == QStringLiteral("linearitySlow")) { c.slowCounts = c.fastCounts = 0; c.result.clear(); }
    }
    // Marks are also accepted from the keyboard (Space) while the player watches the game.
    if (i.kbmCapture && (c.phase == QStringLiteral("deadzone") || c.phase == QStringLiteral("turn")) && i.videoWindow) {
        i.kbmCapture->setWindow(GetAncestor(i.videoWindow, GA_ROOT));
        i.kbmCapture->armRebind([this](std::uint16_t vk) {
            QMetaObject::invokeMethod(this, [this, vk] {
                const auto& phase = impl_->kbmCal.phase;
                if (vk == kbm::code::Space) ps5KbmCalibration(phase == QStringLiteral("deadzone") ? QStringLiteral("deadzoneMark") : QStringLiteral("turnMark"));
                else if (vk == kbm::code::Escape) ps5KbmCalibration(QStringLiteral("cancel"));
                else ps5KbmCalibration(QStringLiteral("rearm"));
            }, Qt::QueuedConnection);
        });
    }
    emit ps5KbmChanged();
}

void QmlPlayerBridge::tickPs5Kbm() {
    auto& i = *impl_;
    if (!i.kbmCapture) return;
    const bool active = ps5KbmSessionActive();
    if (!active && (i.kbmCapture->captured() || i.kbmWantCapture)) {
        i.kbmWantCapture = false;
        i.kbmCapture->capture(false);
    }
    if (!active && !i.kbmCal.phase.isEmpty() && !i.kbmCal.phase.startsWith(QStringLiteral("linearity"))) ps5KbmCalibration(QStringLiteral("cancel"));
    if (active && i.kbmWantCapture && !i.kbmCapture->captured()) ps5KbmCapture(true);
    if (i.kbmCal.phase == QStringLiteral("deadzone")) {
        const auto now = remoteplay::monotonic100ns();
        if (i.kbmCal.sweep.finishedWithoutMark(now)) {
            i.kbm->setRightStickOverride(std::nullopt); i.kbmCal.phase.clear();
            i.kbmMessage = tr("推到 0.6 仍未标记：游戏死区可能更大或镜头被锁定，请在可自由转动视角处重试");
            emit ps5KbmChanged();
        } else i.kbm->setRightStickOverride(std::array<float, 2>{i.kbmCal.sweep.deflection(now), 0.f});
    }
    if (++i.kbmStatsTicks >= 8) { // the bridge tick is ~33 ms: statistics every ~250 ms
        i.kbmStatsTicks = 0;
        emit ps5KbmStatsChanged();
    }
}

#else
QVariantMap QmlPlayerBridge::ps5Kbm() const { return {{"available", false}}; }
QVariantMap QmlPlayerBridge::ps5KbmStats() const { return {}; }
void QmlPlayerBridge::ps5KbmLoad() {}
void QmlPlayerBridge::ps5KbmSetEnabled(bool) {}
bool QmlPlayerBridge::ps5KbmCapture(bool) { return false; }
void QmlPlayerBridge::ps5KbmSetMode(int, int) {}
void QmlPlayerBridge::ps5KbmSelectProfile(int) {}
bool QmlPlayerBridge::ps5KbmSet(const QString&, const QVariant&) { return false; }
bool QmlPlayerBridge::ps5KbmSetBinding(int, const QVariantMap&) { return false; }
void QmlPlayerBridge::ps5KbmAddBinding() {}
void QmlPlayerBridge::ps5KbmRemoveBinding(int) {}
void QmlPlayerBridge::ps5KbmRebind(int, const QString&) {}
void QmlPlayerBridge::ps5KbmRebindMouse(int, int) {}
void QmlPlayerBridge::ps5KbmCancelRebind() {}
void QmlPlayerBridge::ps5KbmProfileAction(const QString&, const QString&) {}
bool QmlPlayerBridge::ps5KbmExport(const QUrl&, bool) { return false; }
bool QmlPlayerBridge::ps5KbmImport(const QUrl&) { return false; }
void QmlPlayerBridge::ps5KbmCalibration(const QString&, double) {}
void QmlPlayerBridge::tickPs5Kbm() {}
void QmlPlayerBridge::setupPs5Kbm() {}
void QmlPlayerBridge::savePs5Kbm() {}
bool QmlPlayerBridge::ps5KbmSessionActive() const { return false; }
#endif
} // namespace veyra::ui
