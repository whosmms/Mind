#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QUuid>
#include "domain/ProjectFactory.h"
#include "services/FileService.h"
#include "services/ProjectJson.h"

using namespace lingya;

static ID freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

static Project example(const QString &kind, const QString &impact)
{
    Project project = ProjectFactory::createMindmap(QStringLiteral("跨表剪切增量演示"));
    auto &source = std::get<TreeSheet>(project.sheets.first());
    source.name = QStringLiteral("来源导图");
    const ID sourceId = source.id;
    TreeNode moving;
    moving.id = freshId();
    moving.parentId = source.rootId;
    moving.title = QStringLiteral("待移动主题");
    moving.contentRevision = 1;
    source.nodes.insert(moving.id, moving);
    TreeNode child;
    child.id = freshId();
    child.parentId = moving.id;
    child.title = QStringLiteral("学习记录所指子主题");
    child.contentRevision = 1;
    source.nodes.insert(child.id, child);
    TreeNode other;
    other.id = freshId();
    other.parentId = source.rootId;
    other.order = 1;
    other.title = QStringLiteral("留在来源表");
    other.contentRevision = 1;
    source.nodes.insert(other.id, other);
    Task task;
    task.id = freshId();
    task.title = moving.title;
    task.source = TaskSource{sourceId, moving.id};
    task.progress = 40;
    project.tasks.append(task);
    ReviewCard card;
    card.id = freshId();
    card.sheetId = sourceId;
    card.nodeId = child.id;
    card.front = QStringLiteral("移动后学习记录还在吗？");
    card.back = QStringLiteral("保留原卡片、已掌握状态与复习时间。");
    card.status = QStringLiteral("mastered");
    card.lastReviewedAt = QDateTime::fromString(QStringLiteral("2026-11-06T09:30:00Z"), Qt::ISODate);
    project.cards.append(card);
    if (impact == QStringLiteral("route"))
    {
        PresentationRoute route;
        route.id = freshId();
        route.sheetId = sourceId;
        route.title = QStringLiteral("必须保留的讲解路线");
        route.steps.append({freshId(), moving.id, QStringLiteral("这条路线引用待移动主题")});
        project.routes.append(route);
    }
    if (impact == QStringLiteral("draft"))
    {
        TextDraft draft;
        draft.id = freshId();
        draft.sourceSheetId = sourceId;
        draft.title = QStringLiteral("必须保留来源的文稿");
        draft.createdAt = QDateTime::fromString(QStringLiteral("2026-11-06T09:30:00Z"), Qt::ISODate);
        draft.sections.append({QStringLiteral("来源节"), QStringLiteral("原内容"), {moving.id},
                               QStringLiteral("source")});
        project.drafts.append(draft);
    }
    project.sheets.append(std::get<Sheet>(ProjectFactory::createSheet(kind, QStringLiteral("目标表"))));
    return project;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() < 2) return 1;
    const QDir directory(application.arguments().at(1));
    if (application.arguments().size() == 3 && application.arguments().at(2) == QStringLiteral("--verify-existing"))
    {
        const QStringList paths = directory.entryList({QStringLiteral("*.lingya")}, QDir::Files);
        for (const QString &name : paths)
        {
            const auto loaded = FileService::load(directory.filePath(name));
            if (const auto *error = std::get_if<AppError>(&loaded)) { qCritical() << error->message; return 5; }
            qInfo().noquote() << name << QStringLiteral("严格加载通过");
        }
        return paths.size() == 10 ? 0 : 6;
    }
    const QStringList names{QStringLiteral("08-跨表保留学习记录_导图.lingya"),
                            QStringLiteral("09-跨表保留学习记录_鱼骨.lingya"),
                            QStringLiteral("10-讲解路线阻断.lingya"),
                            QStringLiteral("11-文稿引用阻断.lingya")};
    for (int i = 0; i < names.size(); ++i)
    {
        const Project project = example(i == 1 ? QStringLiteral("fishbone") : QStringLiteral("mindmap"),
                                        i == 2 ? QStringLiteral("route")
                                        : i == 3 ? QStringLiteral("draft") : QString());
        const QString path = directory.filePath(names.at(i));
        const auto saved = FileService::save(path, project);
        if (const auto *error = std::get_if<AppError>(&saved)) { qCritical() << error->message; return 2; }
        const auto loaded = FileService::load(path);
        if (const auto *error = std::get_if<AppError>(&loaded)) { qCritical() << error->message; return 3; }
        if (ProjectJson::toJson(std::get<Project>(loaded)) != ProjectJson::toJson(project)) return 4;
        qInfo().noquote() << names.at(i) << QStringLiteral("严格保存重开一致");
    }
    return 0;
}
