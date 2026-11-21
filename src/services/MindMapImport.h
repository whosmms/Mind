#pragma once

#include "domain/Domain.h"

namespace lingya::MindMapImport
{
struct Result
{
    Project project;
    int topics = 0;
    int skippedImages = 0;
};

// 将外部导图转换为新工程，原文件只读。调用方负责未保存保护和另存为。
bool supports(const QString &path);
std::variant<Result, AppError> load(const QString &path);
} // namespace lingya::MindMapImport
