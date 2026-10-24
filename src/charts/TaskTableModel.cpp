#include "charts/TaskTableModel.h"

namespace lingya {

TaskTableModel::TaskTableModel(QObject* parent) : QAbstractTableModel(parent) {
}

void TaskTableModel::setTasks(const QVector<Task>& tasks) {
    beginResetModel();
    m_tasks = tasks;
    endResetModel();
}

const QVector<Task>& TaskTableModel::tasks() const {
    return m_tasks;
}

int TaskTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_tasks.size();
}

int TaskTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TaskTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) {
        return QVariant();
    }
    const Task& task = m_tasks.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TitleColumn:
            return task.milestone ? QStringLiteral("◆ %1").arg(task.title) : task.title;
        case OwnerColumn:
            return task.owner;
        case StartColumn:
            return task.startDate.has_value()
                       ? task.startDate->toString(QStringLiteral("yyyy-MM-dd"))
                       : QString();
        case EndColumn:
            return task.endDate.has_value()
                       ? task.endDate->toString(QStringLiteral("yyyy-MM-dd"))
                       : QString();
        case ProgressColumn:
            return QStringLiteral("%1%").arg(task.progress);
        default:
            return QVariant();
        }
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ProgressColumn) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::UserRole) {
        return task.id;   // 供定位与命令使用
    }
    return QVariant();
}

QVariant TaskTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation != Qt::Horizontal) {
        return QVariant(section + 1);   // 行号
    }
    switch (section) {
    case TitleColumn:
        return QStringLiteral("名称");
    case OwnerColumn:
        return QStringLiteral("负责人");
    case StartColumn:
        return QStringLiteral("开始");
    case EndColumn:
        return QStringLiteral("结束");
    case ProgressColumn:
        return QStringLiteral("进度");
    default:
        return QVariant();
    }
}

Qt::ItemFlags TaskTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool TaskTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole || index.row() < 0 ||
        index.row() >= m_tasks.size()) {
        return false;
    }
    const Task& task = m_tasks.at(index.row());
    emit editRequested(task.id, index.column(), value);
    return true;   // 请求已发出；实际变更由 TaskCommands 完成并刷新模型
}

}  // namespace lingya
