#pragma once

#include <QList>
#include <QByteArray>
#include <QStringList>
#include <QVariantList>
#include <functional>

namespace gui {

// Keep mount discovery separate from presentation so hotplug behavior can be
// exercised without mounting devices or relying on the host's disk layout.
class FileLocations
{
public:
    enum class Platform { Linux, MacOS, Windows };
    struct Volume {
        QString path;
        QString label;
        QByteArray device;
        bool valid = true;
        bool ready = true;
        bool operator==(const Volume &other) const;
    };
    using VolumeProvider = std::function<QList<Volume>()>;

    static Platform hostPlatform();
    explicit FileLocations(Platform platform = hostPlatform(), VolumeProvider provider = {});
    bool refresh();
    QVariantList places() const;
    bool directoryAvailable(const QString &path) const;

    static QString normalizePath(QString path, Platform platform = hostPlatform());
    static bool containsPath(const QString &parent, const QString &path, Platform platform = hostPlatform());
    static QString parentPath(const QString &path, Platform platform = hostPlatform());
    static QVariantList breadcrumbs(const QString &path, Platform platform = hostPlatform());

private:
    static QString pathRoot(const QString &path, Platform platform);
    Platform m_platform;
    VolumeProvider m_provider;
    QList<Volume> m_volumes;
    QStringList m_removedRoots;
};

}
