#include "commands/AiCommands.h"
#include "commands/CommandService.h"
#include "document/ProjectDocument.h"
#include "domain/Limits.h"
#include "domain/ProjectFactory.h"
#include "domain/TitleRules.h"

#include <QDateTime>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <optional>

namespace lingya {

namespace {

constexpr int kMaxPurposeChars = 32;

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

const TreeSheet* findTree(const Project& project, const ID& sheetId) {
    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            if (tree->id == sheetId) return tree;
        }
    }
    return nullptr;
}

int totalNodeCount(const Project& project) {
    int count = 0;
    for (const Sheet& sheet : project.sheets) {
        if (const auto* tree = std::get_if<TreeSheet>(&sheet)) {
            count += tree->nodes.size();
        } else if (const auto* flow = std::get_if<FlowSheet>(&sheet)) {
            count += flow->nodes.size();
        }
    }
    return count;
}

AppError schemaError(const QString& message) {
    return AppError{QStringLiteral("SCHEMA_INVALID"), message};
}

bool isAiOrigin(const QString& origin) {
    return origin == QStringLiteral("source") || origin == QStringLiteral("supplement");
}

// 把提案树展开成领域节点（规范§10.6）：节点数≤50、深度≤4（根算第1层）、
// 标题1至80码点、备注≤500码点、origin只能是source/supplement。
// 程序生成ID与order，父级按提案层级串联；AI不生成ID或坐标。
bool buildNodes(const GeneratedNode& source, const std::optional<ID>& parentId, int level, int order,
                const AiApplyOptions& options, const QString& model, QVector<TreeNode>& out,
                AppError& error) {
    if (level > Limits::kAiMapMaxDepth) {
        error = schemaError(QStringLiteral("提案深度超过%1层（根算第1层）")
                                .arg(Limits::kAiMapMaxDepth));
        return false;
    }
    if (out.size() >= Limits::kAiMapMaxNodes) {
        error = AppError{QStringLiteral("LIMIT_EXCEEDED"),
                         QStringLiteral("提案节点数超过上限%1").arg(Limits::kAiMapMaxNodes)};
        return false;
    }

    const QString title = source.title.trimmed();
    if (title.isEmpty()) {
        error = schemaError(QStringLiteral("提案存在空标题节点"));
        return false;
    }
    if (TitleRules::codePointCount(title) > Limits::kAiNodeTitleMaxChars) {
        error = schemaError(QStringLiteral("节点标题“%1”超过%2个字符")
                                .arg(title.left(12))
                                .arg(Limits::kAiNodeTitleMaxChars));
        return false;
    }
    const QString note = source.note.trimmed();
    if (TitleRules::codePointCount(note) > Limits::kAiNoteMaxChars) {
        error = schemaError(QStringLiteral("节点备注“%1”超过%2个字符")
                                .arg(title.left(12))
                                .arg(Limits::kAiNoteMaxChars));
        return false;
    }
    if (!isAiOrigin(source.origin)) {
        error = schemaError(QStringLiteral("节点“%1”的origin只能是source或supplement")
                                .arg(title.left(12)));
        return false;
    }

    TreeNode node;
    node.id = newId();
    node.title = title;
    node.note = note;
    node.parentId = parentId;
    node.order = order;
    node.contentRevision = 1;

    Provenance provenance;
    provenance.kind = QStringLiteral("ai");
    provenance.requestId = options.requestId;
    provenance.model = model;
    provenance.sourceParagraphIds = source.sourceParagraphIds;
    provenance.origin = source.origin;
    node.provenance = provenance;

    out.append(node);
    const ID nodeId = node.id;
    for (int i = 0; i < source.children.size(); ++i) {
        if (!buildNodes(source.children.at(i), nodeId, level + 1, i, options, model, out, error))
            return false;
    }
    return true;
}

// 提案整体准备（校验+构造节点），全部通过后才进入事务，避免半棵树落库。
std::variant<QVector<TreeNode>, AppError> prepareNodes(const MapProposal& proposal,
                                                       const std::optional<ID>& parentId,
                                                       int firstOrder,
                                                       const AiApplyOptions& options) {
    QVector<TreeNode> nodes;
    AppError error;
    if (!buildNodes(proposal.root, parentId, 1, firstOrder, options, proposal.model, nodes, error))
        return error;
    return nodes;
}

}  // namespace

AiCommands::AiCommands(CommandService* commands, const ProjectDocument* document)
    : m_commands(commands), m_document(document) {}

std::variant<ID, AppError> AiCommands::applyMapAsSheet(const MapProposal& proposal,
                                                       const AiApplyOptions& options) {
    const Project& project = m_document->project();
    if (options.expectedRevision >= 0 && m_document->sessionRevision() != options.expectedRevision) {
        return AppError{QStringLiteral("STALE_SOURCE"),
                        QStringLiteral("导图在生成后已被修改，请重新生成或确认后再采用")};
    }
    if (project.sheets.size() >= Limits::kMaxSheets) {
        return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                        QStringLiteral("工作表数量已达上限%1").arg(Limits::kMaxSheets)};
    }

    QString sheetName = options.sheetName.trimmed();
    if (sheetName.isEmpty()) sheetName = proposal.title.trimmed();
    if (sheetName.isEmpty()) sheetName = proposal.root.title.trimmed();
    if (sheetName.isEmpty()) sheetName = QStringLiteral("AI 导图");
    if (TitleRules::codePointCount(sheetName) > Limits::kTitleMaxChars) {
        return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                        QStringLiteral("工作表名称超过%1个字符").arg(Limits::kTitleMaxChars)};
    }

    // 用工厂模板取合法的默认样式与工作表骨架，再整棵替换为提案树。
    const auto built = ProjectFactory::createSheet(QStringLiteral("mindmap"), sheetName);
    if (const auto* error = std::get_if<AppError>(&built)) return *error;
    const auto* skeleton = std::get_if<Sheet>(&built);
    const auto* templateTree = (skeleton != nullptr) ? std::get_if<TreeSheet>(skeleton) : nullptr;
    if (templateTree == nullptr) {
        return AppError{QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("无法构造导图工作表")};
    }
    TreeSheet sheet = *templateTree;
    sheet.name = sheetName;

    const auto prepared = prepareNodes(proposal, std::nullopt, 0, options);
    if (const auto* error = std::get_if<AppError>(&prepared)) return *error;
    const QVector<TreeNode> nodes = std::get<QVector<TreeNode>>(prepared);
    if (nodes.isEmpty()) return schemaError(QStringLiteral("提案没有任何节点"));

    sheet.nodes.clear();
    sheet.rootId = nodes.first().id;
    for (const TreeNode& node : nodes) sheet.nodes.insert(node.id, node);

    const ID sheetId = sheet.id;
    const auto result = m_commands->applyEdit(
        QStringLiteral("采用AI导图（新建工作表）"), [sheet, sheetId](Project& target) {
            target.sheets.push_back(sheet);
            target.activeSheetId = sheetId;
        });
    if (const auto* error = std::get_if<AppError>(&result)) return *error;
    return sheetId;
}

std::variant<ID, AppError> AiCommands::applyMapAsBranch(const MapProposal& proposal,
                                                        const ID& sheetId, const ID& parentId,
                                                        const AiApplyOptions& options) {
    const Project& project = m_document->project();
    if (options.expectedRevision >= 0 && m_document->sessionRevision() != options.expectedRevision) {
        return AppError{QStringLiteral("STALE_SOURCE"),
                        QStringLiteral("导图在生成后已被修改，请重新选择目标节点再采用")};
    }
    const TreeSheet* tree = findTree(project, sheetId);
    if (tree == nullptr) {
        return AppError{QStringLiteral("INVALID_ARGUMENT"),
                        QStringLiteral("目标工作表不存在或不是树图")};
    }
    if (!tree->nodes.contains(parentId)) {
        return AppError{QStringLiteral("STALE_SOURCE"),
                        QStringLiteral("目标节点已不存在，请重新选择插入位置")};
    }

    // 追加到现有子节点之后（规范§10.6“作为目标下一个新子节点”）。
    int nextOrder = 0;
    for (auto it = tree->nodes.constBegin(); it != tree->nodes.constEnd(); ++it) {
        if (it.value().parentId.has_value() && *it.value().parentId == parentId)
            nextOrder = std::max(nextOrder, it.value().order + 1);
    }

    const auto prepared = prepareNodes(proposal, parentId, nextOrder, options);
    if (const auto* error = std::get_if<AppError>(&prepared)) return *error;
    const QVector<TreeNode> nodes = std::get<QVector<TreeNode>>(prepared);
    if (nodes.isEmpty()) return schemaError(QStringLiteral("提案没有任何节点"));

    if (totalNodeCount(project) + nodes.size() > Limits::kMaxNodes) {
        return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                        QStringLiteral("插入后节点总数将超过上限%1").arg(Limits::kMaxNodes)};
    }

    const ID rootId = nodes.first().id;
    const auto result = m_commands->applyEdit(
        QStringLiteral("采用AI导图（插入分支）"), [sheetId, nodes](Project& target) {
            for (Sheet& sheet : target.sheets) {
                if (auto* targetTree = std::get_if<TreeSheet>(&sheet)) {
                    if (targetTree->id != sheetId) continue;
                    for (const TreeNode& node : nodes) targetTree->nodes.insert(node.id, node);
                    break;
                }
            }
        });
    if (const auto* error = std::get_if<AppError>(&result)) return *error;
    return rootId;
}

std::variant<ID, AppError> AiCommands::saveDraft(const DraftProposal& proposal,
                                                 const ID& sourceSheetId, const QString& purpose,
                                                 qint64 sourceRevision,
                                                 const AiApplyOptions& options) {
    const Project& project = m_document->project();
    if (options.expectedRevision >= 0 && m_document->sessionRevision() != options.expectedRevision) {
        return AppError{QStringLiteral("STALE_SOURCE"),
                        QStringLiteral("导图在生成后已被修改，请重新生成文字稿再采用")};
    }
    const TreeSheet* tree = findTree(project, sourceSheetId);
    if (tree == nullptr) {
        return AppError{QStringLiteral("INVALID_ARGUMENT"),
                        QStringLiteral("来源工作表不存在或不是导图/鱼骨图")};
    }
    if (tree->kind != QStringLiteral("mindmap") && tree->kind != QStringLiteral("fishbone")) {
        return AppError{QStringLiteral("INVALID_ARGUMENT"),
                        QStringLiteral("第一版只对导图与鱼骨图生成文字稿")};
    }
    if (proposal.sections.isEmpty()) {
        return schemaError(QStringLiteral("文字稿没有任何段落"));
    }

    // 空标题的回退：工程标题（文稿属于工程级产物）→ 来源工作表名 → 固定兜底
    QString title = proposal.title.trimmed();
    if (title.isEmpty()) title = project.title.trimmed();
    if (title.isEmpty()) title = tree->name.trimmed();
    if (title.isEmpty()) title = QStringLiteral("文字稿");
    if (TitleRules::codePointCount(title) > Limits::kTitleMaxChars) {
        return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                        QStringLiteral("文稿标题超过%1个字符").arg(Limits::kTitleMaxChars)};
    }

    QString normalizedPurpose = purpose.trimmed();
    if (normalizedPurpose.isEmpty()) normalizedPurpose = QStringLiteral("summary");
    if (TitleRules::codePointCount(normalizedPurpose) > kMaxPurposeChars) {
        return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                        QStringLiteral("用途标识超过%1个字符").arg(kMaxPurposeChars)};
    }

    QSet<QString> sourceNodes;
    for (auto it = tree->nodes.constBegin(); it != tree->nodes.constEnd(); ++it)
        sourceNodes.insert(it.key());

    QVector<DraftSection> sections;
    for (const DraftSection& raw : proposal.sections) {
        const QString heading = raw.heading.trimmed();
        const QString text = raw.text.trimmed();
        if (heading.isEmpty()) return schemaError(QStringLiteral("文字稿存在空段落标题"));
        if (TitleRules::codePointCount(heading) > Limits::kTitleMaxChars) {
            return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                            QStringLiteral("段落标题“%1”超过%2个字符")
                                .arg(heading.left(12))
                                .arg(Limits::kTitleMaxChars)};
        }
        if (text.isEmpty()) {
            return schemaError(QStringLiteral("段落“%1”没有正文").arg(heading.left(12)));
        }
        if (TitleRules::codePointCount(text) > Limits::kNoteMaxChars) {
            return AppError{QStringLiteral("LIMIT_EXCEEDED"),
                            QStringLiteral("段落“%1”超过%2个字符")
                                .arg(heading.left(12))
                                .arg(Limits::kNoteMaxChars)};
        }
        if (!isAiOrigin(raw.origin)) {
            return schemaError(QStringLiteral("段落“%1”的origin只能是source或supplement")
                                   .arg(heading.left(12)));
        }
        for (const ID& nodeId : raw.sourceNodeIds) {
            if (!sourceNodes.contains(nodeId)) {
                return AppError{QStringLiteral("INVALID_ARGUMENT"),
                                QStringLiteral("段落“%1”引用了不存在的来源节点").arg(heading.left(12))};
            }
        }
        DraftSection section;
        section.heading = heading;
        section.text = text;
        section.sourceNodeIds = raw.sourceNodeIds;
        section.origin = raw.origin;
        sections.append(section);
    }

    TextDraft draft;
    draft.id = newId();
    draft.sourceSheetId = sourceSheetId;
    draft.title = title;
    draft.sourceRevision = sourceRevision >= 0 ? sourceRevision : m_document->sessionRevision();
    draft.purpose = normalizedPurpose;
    draft.sections = sections;
    draft.createdAt = QDateTime::currentDateTimeUtc();

    const ID draftId = draft.id;
    const auto result = m_commands->applyEdit(QStringLiteral("保存AI文字稿"),
                                              [draft](Project& target) {
                                                  target.drafts.push_back(draft);
                                              });
    if (const auto* error = std::get_if<AppError>(&result)) return *error;
    return draftId;
}

}  // namespace lingya
