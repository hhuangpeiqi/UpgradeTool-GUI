#include "gui/backend/QualcommController.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QSet>
#include <climits>
#include <algorithm>
namespace gui {
namespace {
const QStringList contexts = {"flash", "raw", "chipinfo", "reset", "ramdump", "kickstart", "zip", "archive", "digests", "provision"};
const QStringList booleans = {"packageBuild", "archiveContents", "useImages", "debug", "dryRun", "allowMissing", "allowFusing", "finalize", "skipReset", "skipblock"};
bool commonKey(const QString &key) { return key == "storage" || key == "transport" || key == "debug"; }
QVariant normalized(const QString &key, const QVariant &value) {
    if (booleans.contains(key)) return value.toBool();
    if (key == "xmlFiles") return value.toStringList();
    if (key == "storage" || key == "transport") {
        bool ok = false; int n = value.toInt(&ok);
        if (key == "storage") return ok && n >= 1 && n <= 5 ? n : 3;
        if (!ok || (n != -1 && n != 0 && n != 2)) n = -1;
#ifndef Q_OS_WIN
        // The QDLoader (QUD) transport is Windows-only; drop a value restored
        // from a configuration written on that platform.
        if (n == 2) n = -1;
#endif
        return n;
    }
    return value.toString();
}
QString contextFor(core::Operation op) {
    switch (op) {
    case core::Operation::QualcommFlash: return "flash";
    case core::Operation::QualcommRaw: return "raw";
    case core::Operation::QualcommChipInfo: return "chipinfo";
    case core::Operation::QualcommReset: return "reset";
    case core::Operation::QualcommRamdump: return "ramdump";
    case core::Operation::QualcommKickstart: return "kickstart";
    case core::Operation::QualcommCreateZip: return "zip";
    case core::Operation::QualcommCreateArchive: return "archive";
    case core::Operation::QualcommCreateDigests: return "digests";
    case core::Operation::QualcommProvision: return "provision";
    default: return {};
    }
}
bool readable(const std::string &path) {
    QFileInfo f(QString::fromStdString(path)); return f.isFile() && f.isReadable();
}
}
QualcommController::QualcommController(QObject *parent) : QObject(parent), m_results(this) {
    m_common = {{"storage", 3}, {"transport", -1}, {"debug", false}};
    for (const auto &context : contexts) {
        m_contextResults[context] = new KeyValueModel(this);
        QVariantMap values;
        for (const auto &key : booleans) if (!commonKey(key)) values[key] = false;
        values["xmlFiles"] = QStringList();
        m_profiles[context] = values;
    }
    m_profiles["reset"]["resetMethod"] = "sahara";
    for (auto &profile : m_profiles) { profile["allowFusing"] = false; profile["finalize"] = false; }
    inspect();
}
QVariantMap QualcommController::settingsFor(const QString &context) const {
    QVariantMap result = m_profiles.value(context);
    for (auto it = m_common.begin(); it != m_common.end(); ++it) result[it.key()] = it.value();
    return result;
}
QStringList QualcommController::xmlFilesFor(const QString &context) const { return m_profiles.value(context).value("xmlFiles").toStringList(); }
void QualcommController::retranslate() { inspect(); emit changed(); }
void QualcommController::setValue(const QString &key, const QVariant &value) { setContextValue("flash", key, value); }
void QualcommController::setContextValue(const QString &context, const QString &key, const QVariant &value) {
    if (m_locked || !contexts.contains(context)) return;
    QVariant v = normalized(key, value);
    auto &values = commonKey(key) ? m_common : m_profiles[context];
    if (values.value(key) == v) return;
    values[key] = v;
    // A newly selected primary input must not retain hidden firmware overrides.
    if (key == "programmer" || key == "package") {
        auto &profile = m_profiles[context];
        profile["images"] = QString(); profile["useImages"] = false;
        profile["selector"] = QString();
    }
    m_operationErrors.remove(context);
    if (key == "xmlFiles") inspect();
    emit changed();
}
QString QualcommController::xmlFolder(const QString &context) const {
    // The primary input is the anchor, as in QFIL: the XML picker opens next to
    // the programmer (or the package, or the first Sahara image) instead of a
    // separately chosen firmware folder.
    const auto values = settingsFor(context);
    if (values.value("useImages").toBool()) {
        const auto lines = values.value("images").toString().split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            const int colon = lines.first().indexOf(':');
            if (colon >= 0) {
                const QString path = lines.first().mid(colon + 1).trimmed();
                if (!path.isEmpty()) return QFileInfo(path).absolutePath();
            }
        }
    }
    const QString programmer = values.value("programmer").toString();
    if (!programmer.isEmpty()) return QFileInfo(programmer).absolutePath();
    const QString package = values.value("package").toString();
    return package.isEmpty() ? QString() : QFileInfo(package).absolutePath();
}
QString QualcommController::validateXmlSelection(const QStringList &paths, bool patches) const {
    if (!patches && paths.isEmpty()) return tr("Select at least one rawprogram XML");
    for (const auto &path : paths) {
        QFile file(path);
        if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) return tr("Cannot open %1").arg(path);
        QXmlStreamReader xml(&file);
        bool expected = false, wrong = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            const auto name = xml.name();
            if (name == QLatin1String("ufs")) wrong = true;
            const bool program = name == QLatin1String("program") || name == QLatin1String("erase") || name == QLatin1String("read");
            const bool patch = name == QLatin1String("patch") || name == QLatin1String("patches");
            expected |= patches ? patch : program;
            wrong |= patches ? program : patch;
        }
        if (xml.hasError()) return tr("%1:%2: %3").arg(path).arg(xml.lineNumber()).arg(xml.errorString());
        if (!expected || wrong) return tr("%1 is not a valid %2 XML").arg(path, patches ? QStringLiteral("patch") : QStringLiteral("rawprogram"));
    }
    return {};
}
bool QualcommController::replaceFlashXml(const QStringList &programs, const QStringList &patches) {
    if (m_locked) return false;
    QString error = validateXmlSelection(programs, false);
    if (error.isEmpty()) error = validateXmlSelection(patches, true);
    if (!error.isEmpty()) { emit errorRequested(error); return false; }
    QStringList files;
    for (auto group : {programs, patches}) {
        for (auto &path : group) path = QFileInfo(path).canonicalFilePath();
        std::sort(group.begin(), group.end(), [](const QString &a, const QString &b) {
            const int order = QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(), Qt::CaseInsensitive);
            return order ? order < 0 : a < b;
        });
        for (const auto &path : group) if (!files.contains(path)) files.append(path);
    }
    setContextValue("flash", "xmlFiles", files);
    return true;
}
void QualcommController::addXml(const QString &path, const QString &context) {
    if (m_locked || path.trimmed().isEmpty() || !contexts.contains(context)) return;
    auto files = xmlFilesFor(context); QString absolute = QFileInfo(path).absoluteFilePath();
    if (!files.contains(absolute)) files.append(absolute);
    setContextValue(context, "xmlFiles", files);
}
void QualcommController::removeXml(int index, const QString &context) {
    auto files = xmlFilesFor(context);
    if (m_locked || index < 0 || index >= files.size()) return;
    files.removeAt(index); setContextValue(context, "xmlFiles", files);
}
void QualcommController::moveXml(int index, int offset, const QString &context) {
    auto files = xmlFilesFor(context); int to = index + offset;
    if (m_locked || index < 0 || index >= files.size() || to < 0 || to >= files.size()) return;
    files.move(index, to); setContextValue(context, "xmlFiles", files);
}
void QualcommController::inspect() {
    m_previews.clear(); m_errors.clear(); m_xmlEntries.clear();
    for (const auto &context : contexts) {
        auto &preview = m_previews[context]; auto &error = m_errors[context];
        const auto files = xmlFilesFor(context);
        QList<bool> patches(files.size(), false);
        if (context == "provision" && files.size() > 1) error = tr("Select exactly one UFS provisioning XML");
        for (int index = 0; index < files.size(); ++index) {
            if (!error.isEmpty()) break;
            const auto &path = files.at(index);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) { error = tr("Cannot open %1").arg(path); break; }
            QXmlStreamReader xml(&file); bool hasUfs = false, hasOrdinary = false, hasPatch = false;
            while (!xml.atEnd()) {
                xml.readNext(); if (!xml.isStartElement()) continue;
                QString type = xml.name().toString();
                if (type == "ufs") hasUfs = true;
                if (type == "patch" || type == "patches") hasPatch = true;
                if (type != "program" && type != "erase" && type != "patch" && type != "read") continue;
                hasOrdinary = true; auto a = xml.attributes();
                preview.append(QVariantMap{{"type",type},{"label",a.value("label").toString()},
                    {"file",a.value("filename").toString()},{"lun",a.value("physical_partition_number").toString()},
                    {"sector",a.value("start_sector").toString()},{"count",a.value("num_partition_sectors").toString()}});
            }
            patches[index] = hasPatch;
            if (xml.hasError()) error = tr("%1:%2: %3").arg(path).arg(xml.lineNumber()).arg(xml.errorString());
            else if ((context == "provision" && (!hasUfs || hasOrdinary)) || (context != "provision" && hasUfs))
                error = tr("UFS configuration and ordinary flashing XML require separate tasks");
        }
        // Entries carry the file name and kind so the UI can list what will be
        // flashed without re-parsing the XML itself.
        QVariantList entries;
        for (int index = 0; index < files.size(); ++index) {
            const auto &path = files.at(index);
            entries.append(QVariantMap{{"path", path}, {"name", QFileInfo(path).fileName()},
                {"kind", patches.at(index) ? QStringLiteral("patch") : QStringLiteral("program")}});
        }
        m_xmlEntries[context] = entries;
    }
}
void QualcommController::loadFirmware(const QString &path) {
    if (m_locked) return;
    QFileInfo info(path); auto &values = m_profiles["flash"];
    for (const auto &key : {"programmer", "images", "selector", "package"}) values[key] = QString();
    values["useImages"] = false; QStringList files;
    if (info.isDir()) {
        QDir dir(info.absoluteFilePath()); values["packageBuild"] = false;
        auto programmers = dir.entryList({"*firehose*.mbn", "*firehose*.elf"}, QDir::Files, QDir::Name);
        values["programmer"] = programmers.size() == 1 ? dir.absoluteFilePath(programmers.first()) : QString();

    } else { values["package"] = info.absoluteFilePath(); values["packageBuild"] = true; }
    values["xmlFiles"] = files; m_operationErrors.remove("flash");
    inspect(); emit changed();
}
QString QualcommController::buildRequest(int operation, const QVariantMap &overrides, core::OperationRequest &request) const {
    const auto op = static_cast<core::Operation>(operation); const auto context = contextFor(op);
    if (context.isEmpty()) return tr("Invalid Qualcomm operation");
    auto values = settingsFor(context);
    for (auto i = overrides.begin(); i != overrides.end(); ++i) values[i.key()] = i.value();
    request.vendor = core::Vendor::Qualcomm; request.operation = op; auto &o = request.qualcomm;
    auto text = [&](const char *key) { return values.value(key).toString().trimmed().toStdString(); };
    const bool flash = context == "flash" || context == "digests";
    const bool provision = context == "provision", raw = context == "raw";
    const bool firehose = flash || raw || provision;
    const bool device = firehose || context == "kickstart" || context == "chipinfo" || context == "reset" || context == "ramdump";
    const bool package = context == "zip" || (context == "archive" && values.value("archiveContents").toBool()) ||
        (flash && values.value("packageBuild").toBool());
    const bool resetLoader = context == "reset" && values.value("resetMethod").toString() == "firehose";
    const bool loader = !package && (firehose || context == "kickstart" || context == "archive" || resetLoader);
    bool valid = true;
    auto integer = [&](const char *key, long long fallback, long long minimum, long long maximum) {
        QString t = values.value(key).toString().trimmed(); bool ok = true;
        long long n = t.isEmpty() ? fallback : t.toLongLong(&ok, t.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
        if (!ok || n < minimum || n > maximum) valid = false;
        return n;
    };
    o.debug = values.value("debug").toBool();
    if (device) {
        int transport = int(integer("transport", -1, -1, 2)); if (transport == 1) valid = false;
        o.transport = static_cast<core::QualcommTransport>(transport);
        o.chunkSize = static_cast<long>(integer("chunkSize", 0, 0, LONG_MAX));
    }
    if (resetLoader) {
        o.storage = static_cast<core::QualcommStorage>(integer("storage", 3, 1, 5));
        o.slot = int(integer("slot", -1, -1, INT_MAX));
    }
    if (firehose) {
        o.storage = provision ? core::QualcommStorage::Ufs : static_cast<core::QualcommStorage>(integer("storage", 3, 1, 5));
        o.slot = int(integer("slot", -1, -1, INT_MAX));
        o.skipReset = values.value("skipReset").toBool();
        o.dryRun = context == "digests" || values.value("dryRun").toBool();
        if (!provision) {
            o.skipblock = values.value("skipblock").toBool(); o.allowMissing = values.value("allowMissing").toBool();
            o.allowFusing = values.value("allowFusing").toBool();
            if (context != "digests") o.vipTables = text("vipTables");
        } else o.finalize = values.value("finalize").toBool();
    }
    if (package) {
        o.package = text("package"); o.selector = text("selector"); o.packageBuild = flash;
        if (!readable(o.package)) return tr("Select a readable build package");
    }
    if (loader) {
        if (values.value("useImages").toBool()) {
            QSet<unsigned int> ids;
            for (const auto &line : values.value("images").toString().split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts)) {
                int colon = line.indexOf(':'); bool ok = false; unsigned int id = line.left(colon).trimmed().toUInt(&ok);
                const auto path = line.mid(colon + 1).trimmed().toStdString();
                if (colon < 1 || !ok || !id || id >= 128 || ids.contains(id) || !readable(path))
                    return tr("Invalid, duplicate or unreadable Sahara image mapping");
                ids.insert(id); o.images.push_back({id, path});
            }
            if (o.images.empty()) return tr("Select Sahara images");
        } else {
            o.programmer = text("programmer");
            if (!readable(o.programmer)) return tr("Select a readable programmer");
        }
    }
    if ((flash && !package) || provision) {
        const auto files = xmlFilesFor(context);
        if (files.isEmpty()) return tr("Select XML files");
        if (!m_errors.value(context).isEmpty()) return m_errors.value(context);
        for (const auto &path : files) o.xml.push_back(path.toStdString());
    }
    if (raw) {
        const auto rows = values.value("rawOperations").toList();
        if (rows.isEmpty()) return tr("Add partition operations");
        for (int i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toMap(); bool ok = false; int type = row.value("type").toInt(&ok);
            if (!ok || type < 0 || type > 4) return tr("Invalid partition operation");
            if (type == 4 && i + 1 != rows.size()) return tr("Firehose reset must be the last operation");
            core::QualcommRawOperation entry; entry.type = static_cast<core::QualcommRawType>(type);
            entry.address = row.value("address").toString().trimmed().toStdString(); entry.file = row.value("file").toString().trimmed().toStdString();
            if ((type != 4 && entry.address.empty()) || (type < 2 && entry.file.empty())) return tr("Specify an address and a file for read/write operations");
            if (type == 1 && !readable(entry.file)) return tr("Select a readable image to write");
            o.raw.push_back(entry);
        }
    }
    if (context == "ramdump" || context == "zip" || context == "archive") {
        o.output = text("output"); if (o.output.empty()) return tr("Select an output path");
    }
    if (context == "ramdump") o.filter = text("filter");
    if (context == "digests") { o.digestDir = text("digestDir"); if (o.digestDir.empty()) return tr("Select a digest output directory"); }
    if (context == "kickstart") {
        o.node = text("node");
        if (!o.node.empty() && o.transport != core::QualcommTransport::Auto) return tr("Device node requires Auto transport");
    }
    if (!valid) return tr("Invalid numeric parameter or image mapping");
    return {};
}
bool QualcommController::canRun(int operation, const QVariantMap &overrides) const {
    return validationError(operation, overrides).isEmpty();
}
bool QualcommController::needsDevice(int operation) const {
    // Mirror core::RequiresEnumeratedDevice() so the UI disables the primary
    // action exactly when the backend would refuse to start without a device.
    const auto op = static_cast<core::Operation>(operation);
    const auto context = contextFor(op);
    if (context.isEmpty()) return false;
    if (op == core::Operation::QualcommCreateZip || op == core::Operation::QualcommCreateArchive ||
        op == core::Operation::QualcommCreateDigests) return false;
    const auto values = settingsFor(context);
    if (values.value("dryRun").toBool()) return false;
    if (op == core::Operation::QualcommKickstart && !values.value("node").toString().trimmed().isEmpty()) return false;
    return true;
}
QString QualcommController::validationError(int operation, const QVariantMap &overrides) const {
    core::OperationRequest request; return buildRequest(operation, overrides, request);
}
void QualcommController::run(int operation, const QVariantMap &overrides) {
    if (m_locked) return;
    inspect(); core::OperationRequest request;
    const QString context = contextFor(static_cast<core::Operation>(operation));
    const QString error = buildRequest(operation, overrides, request);
    m_operationErrors[context] = error; emit changed();
    if (!error.isEmpty()) { emit errorRequested(error); return; }
    m_results.Clear(); m_contextResults[context]->Clear(); emit executeRequested(request);
}
void QualcommController::applyResult(const core::OperationResult &result) {
    QVariantList rows;
    for (const auto &item : result.items) rows.append(QVariantMap{{"label",QString::fromStdString(item.label)},{"value",QString::fromStdString(item.value)}});
    const auto context = contextFor(result.operation);
    m_operationErrors[context] = result.status == core::OperationStatus::Failed ? QString::fromStdString(result.diagnostic) : QString();
    m_results.SetItems(rows);
    if (m_contextResults.contains(context)) m_contextResults[context]->SetItems(rows);
    m_profiles[context]["allowFusing"] = false; m_profiles[context]["finalize"] = false;
    emit changed();
}
}
