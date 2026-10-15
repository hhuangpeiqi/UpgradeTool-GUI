#include "gui/backend/DeviceModel.h"
#include "gui/EngineWorker.h"
#include <QVariantMap>
namespace gui {
QVariant DeviceModel::data(const QModelIndex &index,int role) const {
    if(!index.isValid() || index.row()<0 || index.row()>=m_rows.size()) return {};
    const auto row=m_rows[index.row()].toMap();
    if(role==SnapshotRole) return row;
    if(role==LabelRole) return row.value("mode");
    if(role==KeyRole) return row.value("key");
    return {};
}
QHash<int,QByteArray> DeviceModel::roleNames() const { return {{SnapshotRole,"snapshot"},{LabelRole,"label"},{KeyRole,"key"}}; }
core::DeviceRef DeviceModel::referenceAt(int index) const {
    return index>=0 && index<static_cast<int>(m_references.size())?m_references[index]:core::DeviceRef{};
}
void DeviceModel::setRows(const QVariantList &rows) {
    if(rows==m_rows) return;
    beginResetModel(); m_rows=rows; m_keys.clear(); m_references.clear();
    for(const auto &value:rows) {
        const auto row=value.toMap(); m_keys.append(row.value("key").toString());
        m_references.push_back(row.value("reference").value<core::DeviceRef>());
    }
    endResetModel(); emit changed();
}
}
