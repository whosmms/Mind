#pragma once
#include "domain/Domain.h"

#include <QAbstractTableModel>
#include <QVector>

namespace lingya {

// 甘特任务表模型（AI规范§6.4、§9.2）：甘特图左侧任务表的 5 列数据模型。
// 名称 / 负责人 / 开始 / 结束 / 进度。只读展示 + 编辑请求信号；
// 实际写入由控制器经 TaskCommands 提交，保证事务与撤销一致。
class TaskTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        TitleColumn = 0,
        OwnerColumn = 1,
        StartColumn = 2,
        EndColumn = 3,
        ProgressColumn = 4,
        ColumnCount = 5
    };

    explicit TaskTableModel(QObject* parent = nullptr);

    void setTasks(const QVector<Task>& tasks);
    const QVector<Task>& tasks() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

signals:
    // 用户编辑请求：taskId + 列 + 新值；控制器用 TaskCommands 提交（可撤销）
    void editRequested(const ID& taskId, int column, const QVariant& value);

private:
    QVector<Task> m_tasks;
};

}  // namespace lingya
