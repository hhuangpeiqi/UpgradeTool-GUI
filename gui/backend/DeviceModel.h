#ifndef GUI_DEVICE_MODEL_H
#define GUI_DEVICE_MODEL_H
#include <QAbstractListModel>
#include <QVariantList>
#include <QStringList>
#include "domain/Device.h"
namespace gui {
class DeviceModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    enum Role { SnapshotRole=Qt::UserRole+1,LabelRole,KeyRole };
    explicit DeviceModel(QObject *parent=nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent=QModelIndex()) const override { return parent.isValid()?0:m_rows.size(); }
    int count() const { return m_rows.size(); }
    QVariant data(const QModelIndex &,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    const QVariantList &rows() const { return m_rows; }
    const QVariantList &allRows() const { return m_all; }
    const QStringList &keys() const { return m_keys; }
    core::DeviceRef referenceAt(int index) const;
    void setSnapshot(const QVariantList &snapshot) { m_all=snapshot; }
    void setRows(const QVariantList &);
signals:
    void changed();
private:
    QVariantList m_all,m_rows;
    QStringList m_keys;
    std::vector<core::DeviceRef> m_references;
};
}
#endif
