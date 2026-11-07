#include "ai/AiMaterial.h"
#include "services/FileService.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cstdio>

// 候选样例只读检查器：复用生产解析/校验和来源指纹，不把工具检查数计入QtTest套件。
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 4 || args[1] != "--report")
    {
        std::fputs("usage: check_projects --report report.json project...\n", stderr);
        return 2;
    }
    const QFileInfo report(args[2]);
    if (report.suffix().compare("json", Qt::CaseInsensitive) != 0)
        return 2;
    for (int i = 3; i < args.size(); ++i)
    {
        const QFileInfo input(args[i]);
        if (report.absoluteFilePath().compare(input.absoluteFilePath(), Qt::CaseInsensitive) == 0 ||
            (!report.canonicalFilePath().isEmpty() &&
             report.canonicalFilePath().compare(input.canonicalFilePath(), Qt::CaseInsensitive) ==
                 0))
            return 2;
    }
    QJsonArray records;
    bool success = true;
    for (int i = 3; i < args.size(); ++i)
    {
        const auto loaded = lingya::FileService::load(args[i]);
        QJsonObject record{{"path", QFileInfo(args[i]).absoluteFilePath()}};
        if (const auto *error = std::get_if<lingya::AppError>(&loaded))
        {
            record["accepted"] = false;
            record["errorCode"] = error->code;
            success = false;
        }
        else
        {
            const auto &project = std::get<lingya::Project>(loaded);
            record["accepted"] = true;
            record["sheets"] = int(project.sheets.size());
            QJsonArray hashes;
            for (const auto &draft : project.drafts)
            {
                QJsonObject hash{{"draftId", draft.id},
                                 {"hasSourceHash", draft.sourceHash.has_value()}};
                for (const auto &sheet : project.sheets)
                    if (const auto *tree = std::get_if<lingya::TreeSheet>(&sheet);
                        tree && tree->id == draft.sourceSheetId && draft.sourceHash)
                        hash["sourceMatches"] =
                            *draft.sourceHash == lingya::AiMaterial::treeHash(*tree);
                hashes.append(hash);
            }
            record["draftSources"] = hashes;
        }
        records.append(record);
    }
    QSaveFile file(report.absoluteFilePath());
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(QJsonObject{{"success", success},
                                                 {"checked", int(records.size())},
                                                 {"projects", records}})
                           .toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return 3;
    std::printf("checked=%d success=%s\n", int(records.size()), success ? "true" : "false");
    return success ? 0 : 1;
}
