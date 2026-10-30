#include "commands/SheetCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "domain/TitleRules.h"

#include <algorithm>
#include <variant>

namespace lingya {

namespace {

const SheetBase* findSheet(const Project& project, const ID& sheetId) {
    for (const Sheet& sheet : project.sheets) {
        const SheetBase* base = std::visit(
            [](const auto& value) -> const SheetBase* { return &value; }, sheet);
        if (base->id == sheetId) return base;
    }
    return nullptr;
}

QString sheetIdOf(const Sheet& sheet) {
    return std::visit([](const auto& value) { return value.id; }, sheet);
}

}  // namespace

SheetCommands::SheetCommands(CommandService* commands, const ProjectDocument* document)
    : m_commands(commands), m_document(document) {}

std::variant<ID, AppError> SheetCommands::createSheet(const QString& kind, const QString& name) {
    if (m_document->project().sheets.size() >= Limits::kMaxSheets) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("工作表数量已达上限%1").arg(Limits::kMaxSheets)};
    }
    const Project& project = m_document->project();
    const QString trimmedKind = kind.trimmed();
    if (trimmedKind != QStringLiteral("mindmap") && trimmedKind != QStringLiteral("fishbone") &&
        trimmedKind != QStringLiteral("flowchart") && trimmedKind != QStringLiteral("gantt")) {
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("图表类型只允许mindmap/fishbone/flowchart/gantt")};
    }

    QString sheetName = name.trimmed();
    if (sheetName.isEmpty()) {
        sheetName = QStringLiteral("工作表 %1").arg(project.sheets.size() + 1);
    }
    if (TitleRules::codePointCount(sheetName) > Limits::kTitleMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("工作表名称超过%1个字符").arg(Limits::kTitleMaxChars)};
    }

    const auto built = ProjectFactory::createSheet(trimmedKind, sheetName);
    if (const auto* error = std::get_if<AppError>(&built)) {
        return *error;
    }
    const Sheet sheet = std::get<Sheet>(built);
    const ID sheetId = sheetIdOf(sheet);

    const auto result = m_commands->applyEdit(
        QStringLiteral("新增工作表"), [sheet, sheetId](Project& target) {
            target.sheets.push_back(sheet);
            target.activeSheetId = sheetId;
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return sheetId;
}

std::variant<bool, AppError> SheetCommands::renameSheet(const ID& sheetId, const QString& name) {
    if (sheetId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID不能为空")};
    }
    const SheetBase* current = findSheet(m_document->project(), sheetId);
    if (current == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("工作表不存在：%1").arg(sheetId)};
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表名称不能为空白")};
    }
    if (TitleRules::codePointCount(trimmed) > Limits::kTitleMaxChars) {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("工作表名称超过%1个字符").arg(Limits::kTitleMaxChars)};
    }
    if (trimmed == current->name) return true;   // 无操作短路

    const auto result = m_commands->applyEdit(
        QStringLiteral("重命名工作表"), [sheetId, trimmed](Project& project) {
            for (Sheet& sheet : project.sheets) {
                SheetBase* base = std::visit(
                    [](auto& value) -> SheetBase* { return &value; }, sheet);
                if (base->id != sheetId) continue;
                base->name = trimmed;
                return;
            }
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> SheetCommands::changeSheetKind(const ID& sheetId,
                                                            const QString& kind) {
    if (sheetId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID不能为空")};
    }
    if (findSheet(m_document->project(), sheetId) == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("工作表不存在：%1").arg(sheetId)};
    }
    const TreeSheet* current = nullptr;
    for (const Sheet& sheet : m_document->project().sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            if (tree->id == sheetId) current = tree;
        }
    }
    if (current == nullptr) {
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("只能切换导图与鱼骨图；流程图与甘特图是独立模型，"
                                       "不做无损换结构")};
    }
    const QString trimmedKind = kind.trimmed();
    if (trimmedKind != QStringLiteral("mindmap") && trimmedKind != QStringLiteral("fishbone")) {
        return AppError{"INVALID_ARGUMENT",
                        QStringLiteral("只能切换导图与鱼骨图；流程图与甘特图是独立模型")};
    }
    if (current->kind == trimmedKind) return true;   // 无操作短路

    // 换结构保留节点/备注/标签/任务/关系/分组注释/批注（规范§6.2），只改kind与layout
    const bool toFishbone = trimmedKind == QStringLiteral("fishbone");
    const QString nextLayout =
        toFishbone ? QStringLiteral("fishbone")
                   : (current->layout == QStringLiteral("right") ? QStringLiteral("right")
                                                                 : QStringLiteral("bilateral"));

    const auto result = m_commands->applyEdit(
        trimmedKind == QStringLiteral("fishbone") ? QStringLiteral("更换为鱼骨图")
                                                  : QStringLiteral("更换为导图"),
        [sheetId, trimmedKind, nextLayout](Project& project) {
            for (Sheet& sheet : project.sheets) {
                auto* tree = std::get_if<TreeSheet>(&sheet);
                if (tree == nullptr || tree->id != sheetId) continue;
                tree->kind = trimmedKind;
                tree->layout = nextLayout;
                return;
            }
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

std::variant<bool, AppError> SheetCommands::deleteSheet(const ID& sheetId) {
    if (sheetId.isEmpty()) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("工作表ID不能为空")};
    }
    const Project& project = m_document->project();
    if (findSheet(project, sheetId) == nullptr) {
        return AppError{"NOT_FOUND", QStringLiteral("工作表不存在：%1").arg(sheetId)};
    }
    if (project.sheets.size() <= 1) {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("至少要保留一张工作表")};
    }
    // 文稿是用户独立创作的内容，删除工作表不能连带销毁；要求先处理文稿（规范§12影响确认）
    for (const TextDraft& draft : project.drafts) {
        if (draft.sourceSheetId == sheetId) {
            return AppError{"SHEET_IN_USE",
                            QStringLiteral("文稿“%1”仍以该工作表为来源，请先处理文稿再删除")
                                .arg(draft.title)};
        }
    }

    // 单事务：删表 + 任务来源清理 + 卡片来源标记 + 当前表回退
    const auto result = m_commands->applyEdit(
        QStringLiteral("删除工作表"), [sheetId](Project& target) {
            QVector<Sheet> kept;
            for (const Sheet& sheet : target.sheets) {
                if (sheetIdOf(sheet) != sheetId) kept.append(sheet);
            }
            target.sheets = kept;

            // 关联任务保留为独立任务（D-M1-05默认策略），不删除用户数据
            for (Task& task : target.tasks) {
                if (task.source.has_value() && task.source->sheetId == sheetId)
                    task.source.reset();
            }
            // 复习卡片与讲解路线依附于工作表本身（ProjectValidator要求其sheetId必须存在），
            // 随工作表在同一事务内删除；关联任务保留为独立任务，不删除用户排期数据。
            QVector<ReviewCard> keptCards;
            for (const ReviewCard& card : target.cards) {
                if (card.sheetId != sheetId) keptCards.append(card);
            }
            target.cards = keptCards;
            QVector<PresentationRoute> keptRoutes;
            for (const PresentationRoute& route : target.routes) {
                if (route.sheetId != sheetId) keptRoutes.append(route);
            }
            target.routes = keptRoutes;
            if (target.activeSheetId == sheetId && !target.sheets.isEmpty())
                target.activeSheetId = sheetIdOf(target.sheets.first());
        });
    if (const auto* error = std::get_if<AppError>(&result)) {
        return *error;
    }
    return true;
}

}  // namespace lingya
