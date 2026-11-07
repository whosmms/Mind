#pragma once
#include "domain/Domain.h"
namespace lingya::SearchService
{
struct Match
{
    ID sheetId, targetId;
    QString kind, title, context, excerpt;
};
struct Results
{
    QVector<Match> matches;
    int total = 0;
};
// 搜索标题、备注与任务名称，包括折叠节点；只读，不修改筛选和视图。
Results find(const Project &project, const QString &query, int limit = 200);
} // namespace lingya::SearchService
