#include "gui/backend/FileLocations.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QVariantMap>
#include <algorithm>
#include <utility>

namespace gui {

bool FileLocations::Volume::operator==(const Volume &other) const
{
    return path == other.path && label == other.label && device == other.device
        && valid == other.valid && ready == other.ready;
}

FileLocations::Platform FileLocations::hostPlatform()
{
#ifdef Q_OS_WIN
    return Platform::Windows;
#elif defined(Q_OS_MACOS)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}

FileLocations::FileLocations(Platform platform, VolumeProvider provider)
    : m_platform(platform), m_provider(std::move(provider))
{
    if (!m_provider) m_provider = [] {
        QList<Volume> volumes;
        // Request a fresh enumeration on every scan; never retain QStorageInfo's
        // cached readiness or mount data between scans.
        for (const auto &storage : QStorageInfo::mountedVolumes())
            volumes.append({storage.rootPath(), storage.name(), storage.device(),
                            storage.isValid(), storage.isReady()});
        return volumes;
    };
}

QString FileLocations::pathRoot(const QString &path, Platform platform)
{
    if (platform == Platform::Windows) {
        if (path.size() >= 3 && path[0].isLetter() && path[1] == ':' && path[2] == '/')
            return path.left(3);
        if (path.startsWith("//")) {
            const auto parts = path.mid(2).split('/', Qt::SkipEmptyParts);
            if (parts.size() >= 2) return "//" + parts[0] + "/" + parts[1];
        }
    }
    return path.startsWith('/') ? QStringLiteral("/") : QString();
}

QString FileLocations::normalizePath(QString path, Platform platform)
{
    if (path.isEmpty()) return {};
    if (platform == Platform::Windows) {
        path.replace('\\', '/');
        if (path.size() >= 2 && path[1] == ':') path[0] = path[0].toUpper();
    }
    const QString root = pathRoot(path, platform);
    QString clean = QDir::cleanPath(path);
    // Preserve Windows roots even when testing their spelling on a Unix host.
    if (root.startsWith("//") && !clean.startsWith("//")) clean.prepend('/');
    if (!root.isEmpty() && clean.size() < root.size()) clean = root;
    return clean;
}

bool FileLocations::containsPath(const QString &parent, const QString &path, Platform platform)
{
    const QString base = normalizePath(parent, platform);
    const QString target = normalizePath(path, platform);
    if (base.isEmpty() || target.isEmpty()) return false;
    const auto sensitivity = platform == Platform::Windows ? Qt::CaseInsensitive : Qt::CaseSensitive;
    return target.compare(base, sensitivity) == 0
        || target.startsWith(base.endsWith('/') ? base : base + '/', sensitivity);
}

QString FileLocations::parentPath(const QString &path, Platform platform)
{
    const QString clean = normalizePath(path, platform);
    const QString root = pathRoot(clean, platform);
    if (clean == root || clean.isEmpty()) return clean;
    const int slash = clean.lastIndexOf('/');
    return slash < root.size() ? root : clean.left(slash);
}

QVariantList FileLocations::breadcrumbs(const QString &path, Platform platform)
{
    const QString clean = normalizePath(path, platform);
    QString accumulated = pathRoot(clean, platform);
    QVariantList crumbs;
    auto add = [&crumbs](const QString &name, const QString &value) {
        crumbs.append(QVariantMap{{"name", name}, {"path", value}});
    };
    if (!accumulated.isEmpty()) add(accumulated, accumulated);
    const auto parts = clean.mid(accumulated.size()).split('/', Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        if (!accumulated.isEmpty() && !accumulated.endsWith('/')) accumulated += '/';
        accumulated += part;
        add(part, accumulated);
    }
    return crumbs;
}

bool FileLocations::refresh()
{
    QList<Volume> next;
    for (auto volume : m_provider()) {
        if (!volume.valid || !volume.ready || volume.path.isEmpty()) continue;
        volume.path = normalizePath(volume.path, m_platform);
        const auto duplicate = std::find_if(next.begin(), next.end(), [&](const Volume &v) {
            return containsPath(v.path, volume.path, m_platform)
                && containsPath(volume.path, v.path, m_platform);
        });
        if (duplicate == next.end()) next.append(volume);
    }
    std::sort(next.begin(), next.end(), [](const Volume &a, const Volume &b) { return a.path < b.path; });
    if (next == m_volumes) return false;

    auto present = [&](const QString &path) {
        return std::any_of(next.begin(), next.end(), [&](const Volume &v) {
            return containsPath(v.path, path, m_platform) && containsPath(path, v.path, m_platform);
        });
    };
    for (const auto &old : m_volumes)
        if (!present(old.path) && !m_removedRoots.contains(old.path)) m_removedRoots.append(old.path);
    m_removedRoots.erase(std::remove_if(m_removedRoots.begin(), m_removedRoots.end(), present), m_removedRoots.end());
    m_volumes = std::move(next);
    return true;
}

QVariantList FileLocations::places() const
{
    QVariantList places;
    for (const auto &volume : m_volumes) {
        bool visible = m_platform == Platform::Windows;
        const QStringList roots = m_platform == Platform::MacOS
            ? QStringList{QStringLiteral("/Volumes")}
            : QStringList{QStringLiteral("/media"), QStringLiteral("/run/media"), QStringLiteral("/mnt")};
        for (const auto &root : roots)
            visible = visible || containsPath(root, volume.path, m_platform);
        if (!visible) continue;
        QString name = volume.label;
        if (m_platform == Platform::Windows) {
            const QString drive = volume.path.endsWith('/') ? volume.path.chopped(1) : volume.path;
            name = name.isEmpty() ? drive : name + " (" + drive + ')';
        } else if (name.isEmpty()) {
            name = QFileInfo(volume.path).fileName();
        }
        places.append(QVariantMap{{"name", name}, {"path", volume.path}});
    }
    return places;
}

bool FileLocations::directoryAvailable(const QString &path) const
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    for (const auto &root : m_removedRoots)
        if (containsPath(root, path, m_platform) || containsPath(root, canonical, m_platform)) return false;
    return info.isDir() && info.isReadable();
}

}
