#pragma once
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include "domain/Domain.h"   // ID 与 DraftSection 来自领域层（原设计§10.3）

namespace lingya {

// AI服务契约值类型（原设计§10.3，字段逐字采用）
// 所有发到队列连接的自定义类型用Q_DECLARE_METATYPE声明，并在初始化时qRegisterMetaType。
// 注意：Q_DECLARE_METATYPE展开为QMetaTypeId<T>模板特化，必须位于全局作用域，
// 故统一放在本文件末尾并带lingya限定（C++模板特化声明位置规则）。
struct AiUsage {
    qint64 inputTokens = 0, outputTokens = 0, totalTokens = 0;
};
struct AiError {
    QString code, message, requestId;
    bool retryable = false;
};
struct AiStatus {
    bool configured = false, busy = false;
    QString model;
};
struct GenerateMapRequest {
    QString requestId, text, requirements;
    QString mode = "faithful";
    int maxDepth = 4, maxNodes = 50;
};
struct GeneratedNode {
    QString title, note;
    QStringList sourceParagraphIds;
    QString origin = "source";
    QVector<GeneratedNode> children;
};
struct MapProposal {
    QString title;
    GeneratedNode root;
    QStringList warnings;
    AiUsage usage;
    QString model, promptVersion = "map-v1";
};
struct DraftSourceNode {
    ID id;
    std::optional<ID> parentId;
    int order = 0;
    QString title, note;
};
struct DraftSourceRelation {
    ID sourceId, targetId;
    QString label;
};
struct GenerateDraftRequest {
    QString requestId;
    ID sheetId;
    qint64 projectRevision = 0;
    QString purpose = "summary", tone = "plain";
    int targetChars = 400;
    QString mode = "faithful", requirements;
    QVector<DraftSourceNode> source;
    QVector<DraftSourceRelation> relations;
};
struct DraftProposal {
    QString title;
    QVector<DraftSection> sections;
    QStringList warnings;
    AiUsage usage;
    QString model, promptVersion = "draft-v1";
};

// 队列连接自定义类型的注册：main初始化时调用一次（原设计§10.3）
inline void registerAiMetaTypes() {
    qRegisterMetaType<AiUsage>("AiUsage");
    qRegisterMetaType<AiError>("AiError");
    qRegisterMetaType<AiStatus>("AiStatus");
    qRegisterMetaType<GenerateMapRequest>("GenerateMapRequest");
    qRegisterMetaType<MapProposal>("MapProposal");
    qRegisterMetaType<GenerateDraftRequest>("GenerateDraftRequest");
    qRegisterMetaType<DraftProposal>("DraftProposal");
}

}  // namespace lingya

// 元类型声明：特化必须在模板所属命名空间或其外围（全局）声明，不能放进lingya内部
Q_DECLARE_METATYPE(lingya::AiUsage)
Q_DECLARE_METATYPE(lingya::AiError)
Q_DECLARE_METATYPE(lingya::AiStatus)
Q_DECLARE_METATYPE(lingya::GenerateMapRequest)
Q_DECLARE_METATYPE(lingya::MapProposal)
Q_DECLARE_METATYPE(lingya::GenerateDraftRequest)
Q_DECLARE_METATYPE(lingya::DraftProposal)
