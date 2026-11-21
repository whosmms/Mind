#pragma once
#include "ai/AiTypes.h"
#include <QJsonObject>
#include <variant>
namespace lingya::AiMaterial
{
QJsonObject draftJson(const GenerateDraftRequest &request);
QJsonObject mapJson(const QString &text);
QString treeHash(const TreeSheet &sheet);
const TreeSheet *sourceTree(const Project &project, const ID &sheetId);
DraftSourceSnapshot draftSourceSnapshot(const TreeSheet &sheet, const QStringList &nodeIds);
qint64 draftSourceSnapshotCharacters(const DraftSourceSnapshot &snapshot);
// 整表指纹仍匹配时，可用现有原图恢复旧稿缺少的段落来源快照。
bool restoreLegacyDraftSources(Project &project);
std::variant<GenerateDraftRequest, AppError> fromTree(const Project &project, const ID &sheetId,
                                                      const ID &rootId = {});
std::optional<AppError> validateMapInput(const GenerateMapRequest &request);
std::optional<AppError> validateDraftInput(const GenerateDraftRequest &request);
} // namespace lingya::AiMaterial
