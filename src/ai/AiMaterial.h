#pragma once
#include "ai/AiTypes.h"
#include <QJsonObject>
#include <variant>
namespace lingya::AiMaterial
{
QJsonObject draftJson(const GenerateDraftRequest &request);
QJsonObject mapJson(const QString &text);
QString treeHash(const TreeSheet &sheet);
std::variant<GenerateDraftRequest, AppError> fromTree(const Project &project, const ID &sheetId,
                                                      const ID &rootId = {});
std::optional<AppError> validateMapInput(const GenerateMapRequest &request);
std::optional<AppError> validateDraftInput(const GenerateDraftRequest &request);
} // namespace lingya::AiMaterial
