#pragma once
#include <QString>

namespace lingya {

// AI 提示模板库（X05/X06）：带版本号，可从 qrc 覆盖，缺资源时回退内置文本。
// 版本随模板内容变化递增；请求体与日志里带版本号，便于排查“模型输出异常时用的是哪一版提示”。
// 模板正文支持以 # 开头的注释行（版本/日期/变更说明），加载时会被剔除，不发给模型。
class PromptLibrary {
public:
    static constexpr int kVersion = 1;

    // 导图生成提示（模板 map-v1）
    static QString mapPrompt();

    // 文稿生成提示（模板 draft-v1）
    static QString draftPrompt();

    // 读取 qrc 路径并剔除注释行；失败返回空串
    static QString fromResource(const QString& path);
};

}  // namespace lingya
