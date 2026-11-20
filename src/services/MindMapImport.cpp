#include "services/MindMapImport.h"

#include "domain/ProjectFactory.h"
#include "domain/ProjectValidator.h"
#include "domain/TitleRules.h"
#include "services/ImageService.h"
#include "services/ProjectJson.h"
#include "services/XMindArchive.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

namespace lingya::MindMapImport
{
namespace
{
constexpr qint64 kMaxArchiveBytes = 64 * 1024 * 1024;
constexpr int kMaxDepth = 128;

struct Topic
{
    QString title, note, link, sourceId, side, image;
    QStringList tags;
    QVector<Topic> children;
    bool folded = false;
};

struct Connection
{
    QString source, target, text;
};

struct Map
{
    QString title, layout;
    Topic root;
    QVector<Connection> connections;
};

AppError invalid(const QString &message)
{
    return {"INVALID_PROJECT", message};
}

struct ParseState
{
    int count = 0;
    int maxDepth = 0;
    AppError error = invalid(QStringLiteral("导图的主题或工作表结构无效"));

    bool add(int depth)
    {
        maxDepth = qMax(maxDepth, depth);
        if (++count <= Limits::kMaxNodes && depth <= kMaxDepth)
        {
            return true;
        }
        error = {"LIMIT_EXCEEDED", QStringLiteral("导图最多 %1 个主题、%2 层，请先在原软件中拆分")
                                       .arg(Limits::kMaxNodes)
                                       .arg(kMaxDepth)};
        return false;
    }
};

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString treeLayout(const QString &structure)
{
    if (structure.endsWith(".left") || structure.endsWith(".right"))
    {
        return structure.section('.', -1);
    }
    if (structure.endsWith(".up") || structure.endsWith(".down"))
    {
        return structure.section('.', -1);
    }
    return QStringLiteral("bilateral");
}

QString plainText(const QString &text)
{
    return Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
}

bool parseJsonTopic(const QJsonObject &object, Topic &topic, ParseState &state, int depth)
{
    if (!state.add(depth))
    {
        return false;
    }
    topic.title = object.value("title").toString();
    const auto notes = object.value("notes").toObject();
    topic.note = notes.value("plain").toObject().value("content").toString();
    if (topic.note.isEmpty())
    {
        topic.note = plainText(notes.value("html").toObject().value("content").toString());
    }
    topic.link = object.value("href").toString();
    topic.sourceId = object.value("id").toString();
    topic.image = object.value("image").toObject().value("src").toString();
    const auto branch = object.value("branch").toString();
    topic.folded = branch == "folded" || branch == "fold";
    for (const auto label : object.value("labels").toArray())
    {
        if (label.isString())
        {
            topic.tags.append(label.toString());
        }
    }
    const auto children = object.value("children");
    if (!children.isUndefined() && !children.isNull() && !children.isObject())
    {
        return false;
    }
    for (const auto *type : {"attached", "detached"})
    {
        const auto group = children.toObject().value(QLatin1String(type));
        if (group.isUndefined() || group.isNull())
        {
            continue;
        }
        if (!group.isArray())
        {
            return false;
        }
        for (const auto child : group.toArray())
        {
            Topic next;
            if (!child.isObject() || !parseJsonTopic(child.toObject(), next, state, depth + 1))
            {
                return false;
            }
            topic.children.append(std::move(next));
        }
    }
    return true;
}

std::variant<QVector<Map>, AppError> parseXMindJson(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() ||
        document.array().isEmpty())
    {
        return invalid(QStringLiteral("XMind 的 content.json 无效或没有工作表"));
    }
    const auto sheets = document.array();
    if (sheets.size() > Limits::kMaxSheets)
    {
        return AppError{"LIMIT_EXCEEDED",
                        QStringLiteral("一次最多导入 %1 张工作表").arg(Limits::kMaxSheets)};
    }
    QVector<Map> maps;
    ParseState state;
    for (const auto value : sheets)
    {
        const auto sheet = value.toObject();
        const auto root = sheet.value("rootTopic");
        Map map;
        if (!root.isObject() || !parseJsonTopic(root.toObject(), map.root, state, 1))
        {
            return state.error;
        }
        map.title = sheet.value("title").toString();
        map.layout = treeLayout(root.toObject().value("structureClass").toString());
        for (const auto value : sheet.value("relationships").toArray())
        {
            const auto relation = value.toObject();
            map.connections.append({relation.value("end1Id").toString(),
                                    relation.value("end2Id").toString(),
                                    relation.value("title").toString()});
        }
        maps.append(std::move(map));
    }
    return maps;
}

bool openXml(QXmlStreamReader &xml, const char *root)
{
    xml.setEntityExpansionLimit(1024);
    while (!xml.atEnd())
    {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD)
        {
            xml.raiseError(QStringLiteral("导入文件不能包含 DTD"));
            return false;
        }
        if (xml.isStartElement())
        {
            return xml.name() == QLatin1StringView(root);
        }
    }
    return false;
}

bool xmlComplete(QXmlStreamReader &xml)
{
    while (!xml.atEnd())
    {
        xml.readNext();
    }
    return !xml.hasError();
}

QString attribute(const QXmlStreamReader &xml, const char *name)
{
    // XMind 的链接和图片属性可能带命名空间前缀。
    for (const auto &item : xml.attributes())
    {
        if (item.name() == QLatin1StringView(name))
        {
            return item.value().toString();
        }
    }
    return {};
}

QString readLabel(QXmlStreamReader &xml)
{
    return xml.readElementText(QXmlStreamReader::IncludeChildElements);
}

bool parseXMindTopic(QXmlStreamReader &xml, Topic &topic, ParseState &state, int depth)
{
    if (!state.add(depth))
    {
        return false;
    }
    topic.sourceId = attribute(xml, "id");
    topic.link = attribute(xml, "href");
    const auto branch = attribute(xml, "branch");
    topic.folded = branch == "folded" || branch == "fold";
    while (xml.readNextStartElement())
    {
        if (xml.name() == QLatin1StringView("title"))
        {
            topic.title = readLabel(xml);
        }
        else if (xml.name() == QLatin1StringView("notes"))
        {
            while (xml.readNextStartElement())
            {
                if (xml.name() == QLatin1StringView("plain"))
                {
                    topic.note = readLabel(xml);
                }
                else
                {
                    xml.skipCurrentElement();
                }
            }
        }
        else if (xml.name() == QLatin1StringView("labels"))
        {
            while (xml.readNextStartElement())
            {
                if (xml.name() == QLatin1StringView("label"))
                {
                    topic.tags.append(readLabel(xml));
                }
                else
                {
                    xml.skipCurrentElement();
                }
            }
        }
        else if (xml.name() == QLatin1StringView("img"))
        {
            topic.image = attribute(xml, "src");
            xml.skipCurrentElement();
        }
        else if (xml.name() == QLatin1StringView("children"))
        {
            while (xml.readNextStartElement())
            {
                const auto type = attribute(xml, "type");
                if (xml.name() != QLatin1StringView("topics") ||
                    (!type.isEmpty() && type != "attached" && type != "detached"))
                {
                    xml.skipCurrentElement();
                    continue;
                }
                while (xml.readNextStartElement())
                {
                    if (xml.name() != QLatin1StringView("topic"))
                    {
                        xml.skipCurrentElement();
                        continue;
                    }
                    Topic child;
                    if (!parseXMindTopic(xml, child, state, depth + 1))
                    {
                        return false;
                    }
                    topic.children.append(std::move(child));
                }
            }
        }
        else
        {
            xml.skipCurrentElement();
        }
    }
    return !xml.hasError();
}

std::variant<QVector<Map>, AppError> parseXMindXml(const QByteArray &bytes)
{
    QXmlStreamReader xml(bytes);
    if (!openXml(xml, "xmap-content"))
    {
        return invalid(QStringLiteral("旧版 XMind 的 content.xml 无效"));
    }
    QVector<Map> maps;
    ParseState state;
    while (xml.readNextStartElement())
    {
        if (xml.name() != QLatin1StringView("sheet"))
        {
            xml.skipCurrentElement();
            continue;
        }
        if (maps.size() >= Limits::kMaxSheets)
        {
            return AppError{"LIMIT_EXCEEDED",
                            QStringLiteral("一次最多导入 %1 张工作表").arg(Limits::kMaxSheets)};
        }
        Map map;
        bool hasRoot = false;
        while (xml.readNextStartElement())
        {
            if (xml.name() == QLatin1StringView("title"))
            {
                map.title = readLabel(xml);
            }
            else if (xml.name() == QLatin1StringView("topic"))
            {
                map.layout = treeLayout(attribute(xml, "structure-class"));
                if (hasRoot || !parseXMindTopic(xml, map.root, state, 1))
                {
                    return state.error;
                }
                hasRoot = true;
            }
            else if (xml.name() == QLatin1StringView("relationships"))
            {
                while (xml.readNextStartElement())
                {
                    if (xml.name() != QLatin1StringView("relationship"))
                    {
                        xml.skipCurrentElement();
                        continue;
                    }
                    Connection connection{attribute(xml, "end1"), attribute(xml, "end2"), {}};
                    while (xml.readNextStartElement())
                    {
                        if (xml.name() == QLatin1StringView("title"))
                        {
                            connection.text = readLabel(xml);
                        }
                        else
                        {
                            xml.skipCurrentElement();
                        }
                    }
                    map.connections.append(std::move(connection));
                }
            }
            else
            {
                xml.skipCurrentElement();
            }
        }
        if (!hasRoot)
        {
            return state.error;
        }
        maps.append(std::move(map));
    }
    if (!xmlComplete(xml) || maps.isEmpty())
    {
        return invalid(QStringLiteral("旧版 XMind 的 XML 损坏或没有工作表"));
    }
    return maps;
}

bool parseOutline(QXmlStreamReader &xml, Topic &topic, ParseState &state, int depth)
{
    if (!state.add(depth) || !xml.attributes().hasAttribute(QStringLiteral("text")))
    {
        return false;
    }
    topic.title = plainText(attribute(xml, "text"));
    for (const auto *name : {"_note", "note", "description"})
    {
        const auto value = attribute(xml, name);
        if (!value.isEmpty())
        {
            topic.note = plainText(value);
            break;
        }
    }
    for (const auto *name : {"htmlUrl", "url", "xmlUrl"})
    {
        const auto value = attribute(xml, name);
        if (!value.isEmpty())
        {
            topic.link = value;
            break;
        }
    }
    while (xml.readNextStartElement())
    {
        if (xml.name() == QLatin1StringView("outline"))
        {
            Topic child;
            if (!parseOutline(xml, child, state, depth + 1))
            {
                return false;
            }
            topic.children.append(std::move(child));
        }
        else
        {
            xml.skipCurrentElement();
        }
    }
    return !xml.hasError();
}

std::variant<QVector<Map>, AppError> parseOpml(const QByteArray &bytes, const QString &fallback)
{
    QXmlStreamReader xml(bytes);
    if (!openXml(xml, "opml"))
    {
        return invalid(QStringLiteral("OPML 文件的根元素无效"));
    }
    QVector<Topic> outlines;
    ParseState state;
    state.error = invalid(QStringLiteral("OPML 的大纲条目缺少标题或内容不完整"));
    Map map;
    bool body = false;
    while (xml.readNextStartElement())
    {
        if (xml.name() == QLatin1StringView("head"))
        {
            while (xml.readNextStartElement())
            {
                if (xml.name() == QLatin1StringView("title"))
                {
                    map.title = readLabel(xml);
                }
                else
                {
                    xml.skipCurrentElement();
                }
            }
        }
        else if (xml.name() == QLatin1StringView("body") && !body)
        {
            body = true;
            while (xml.readNextStartElement())
            {
                if (xml.name() != QLatin1StringView("outline"))
                {
                    xml.skipCurrentElement();
                    continue;
                }
                Topic node;
                if (!parseOutline(xml, node, state, 1))
                {
                    return state.error;
                }
                outlines.append(std::move(node));
            }
        }
        else
        {
            xml.skipCurrentElement();
        }
    }
    if (!xmlComplete(xml) || outlines.isEmpty())
    {
        return invalid(QStringLiteral("OPML 文件损坏或没有可以导入的大纲"));
    }
    if (outlines.size() == 1)
    {
        map.root = std::move(outlines.first());
    }
    else
    {
        if (!state.add(state.maxDepth + 1))
        {
            return state.error;
        }
        map.root.title = map.title.trimmed().isEmpty() ? fallback : map.title;
        map.root.children = std::move(outlines);
    }
    if (map.title.trimmed().isEmpty())
    {
        map.title = map.root.title;
    }
    return QVector<Map>{std::move(map)};
}

bool blockTag(QStringView name)
{
    return name == QLatin1StringView("p") || name == QLatin1StringView("br") ||
           name == QLatin1StringView("div") || name == QLatin1StringView("li");
}

QString readRichContent(QXmlStreamReader &xml)
{
    int depth = 1;
    QString current;
    QStringList lines;
    const auto flush = [&]
    {
        const auto text = current.simplified();
        if (!text.isEmpty())
        {
            lines.append(text);
        }
        current.clear();
    };
    while (depth > 0 && !xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement())
        {
            const auto name = xml.name();
            if (name == QLatin1StringView("head") || name == QLatin1StringView("style") ||
                name == QLatin1StringView("script"))
            {
                xml.skipCurrentElement();
                continue;
            }
            if (blockTag(name))
            {
                flush();
            }
            ++depth;
        }
        else if (xml.isEndElement())
        {
            if (blockTag(xml.name()))
            {
                flush();
            }
            --depth;
        }
        else if (xml.isCharacters())
        {
            current.append(xml.text());
        }
    }
    flush();
    return lines.join('\n');
}

bool parseFreeMindNode(QXmlStreamReader &xml, Topic &topic, QVector<Connection> &links,
                       ParseState &state, int depth)
{
    if (!state.add(depth))
    {
        return false;
    }
    topic.sourceId = attribute(xml, "ID");
    topic.title = attribute(xml, "TEXT");
    topic.link = attribute(xml, "LINK");
    topic.side = attribute(xml, "POSITION").toLower();
    topic.folded = attribute(xml, "FOLDED").compare("true", Qt::CaseInsensitive) == 0;
    while (xml.readNextStartElement())
    {
        if (xml.name() == QLatin1StringView("node"))
        {
            Topic child;
            if (!parseFreeMindNode(xml, child, links, state, depth + 1))
            {
                return false;
            }
            topic.children.append(std::move(child));
        }
        else if (xml.name() == QLatin1StringView("richcontent"))
        {
            const auto type = attribute(xml, "TYPE");
            const auto content = readRichContent(xml);
            if (type == "NOTE")
            {
                topic.note = content;
            }
            else if (type == "NODE" && !content.isEmpty())
            {
                topic.title = content;
            }
        }
        else if (xml.name() == QLatin1StringView("arrowlink"))
        {
            links.append({topic.sourceId, attribute(xml, "DESTINATION"), {}});
            xml.skipCurrentElement();
        }
        else
        {
            xml.skipCurrentElement();
        }
    }
    return !xml.hasError();
}

std::variant<QVector<Map>, AppError> parseFreeMind(const QByteArray &bytes)
{
    QXmlStreamReader xml(bytes);
    if (!openXml(xml, "map"))
    {
        return invalid(QStringLiteral("FreeMind 文件的根元素无效"));
    }
    Map map;
    ParseState state;
    bool hasRoot = false;
    while (xml.readNextStartElement())
    {
        if (xml.name() == QLatin1StringView("node"))
        {
            if (hasRoot || !parseFreeMindNode(xml, map.root, map.connections, state, 1))
            {
                return state.error;
            }
            hasRoot = true;
        }
        else
        {
            xml.skipCurrentElement();
        }
    }
    if (!xmlComplete(xml) || !hasRoot)
    {
        return invalid(QStringLiteral("FreeMind 文件损坏或缺少根主题"));
    }
    map.title = map.root.title;
    return QVector<Map>{std::move(map)};
}

class EmbeddedImages
{
  public:
    explicit EmbeddedImages(const XMindArchive *archive) : m_archive(archive) {}

    QString take(const QString &reference)
    {
        if (reference.isEmpty() || !m_archive)
        {
            return {};
        }
        auto cached = m_cache.constFind(reference);
        if (cached == m_cache.cend())
        {
            cached = m_cache.insert(reference, decode(reference));
        }
        if (cached->png.isEmpty() || m_bytes + cached->png.size() > 6 * 1024 * 1024 ||
            m_pixels + cached->pixels > 16 * 1024 * 1024)
        {
            ++skipped;
            return {};
        }
        m_bytes += cached->png.size();
        m_pixels += cached->pixels;
        return cached->png;
    }
    int skipped = 0;

  private:
    struct Image
    {
        QString png;
        qint64 pixels = 0;
    };

    Image decode(const QString &reference)
    {
        if (!reference.startsWith("xap:") || m_budget <= 0)
        {
            return {};
        }
        const auto path = reference.mid(4);
        if (path.isEmpty() || path.startsWith('/') || path.contains('\\') ||
            path.split('/').contains(".."))
        {
            return {};
        }
        const auto entry = m_archive->read(path.toUtf8(), qMin<qint64>(m_budget, 10 * 1024 * 1024));
        const auto *bytes = std::get_if<QByteArray>(&entry);
        if (!bytes)
        {
            return {};
        }
        m_budget -= bytes->size();
        QBuffer buffer;
        buffer.setData(*bytes);
        if (!buffer.open(QIODevice::ReadOnly))
        {
            return {};
        }
        QImageReader reader(&buffer);
        const auto format = reader.format();
        if (format != "png" && format != "jpeg" && format != "gif" && format != "bmp")
        {
            return {};
        }
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isEmpty() || qint64(size.width()) * size.height() > 16000000)
        {
            return {};
        }
        if (size.width() > 1024 || size.height() > 1024)
        {
            reader.setScaledSize(size.scaled(1024, 1024, Qt::KeepAspectRatio));
        }
        const auto normalized = ImageService::importImage(reader.read());
        if (const auto *png = std::get_if<QString>(&normalized))
        {
            const auto image = ImageService::decode(*png);
            return {*png, qint64(image.width()) * image.height()};
        }
        return {};
    }

    const XMindArchive *m_archive;
    QHash<QString, Image> m_cache;
    qint64 m_budget = 32 * 1024 * 1024, m_bytes = 0, m_pixels = 0;
};

bool addTopic(TreeSheet &sheet, const Topic &source, const std::optional<ID> &parent, int order,
              QHash<QString, ID> &importIds, EmbeddedImages &images)
{
    TreeNode node;
    node.id = parent ? newId() : sheet.rootId;
    node.title = TitleRules::normalize(source.title);
    node.note = source.note;
    if (TitleRules::exceedsTitleLimit(node.title) ||
        TitleRules::codePointCount(node.note) > Limits::kNoteMaxChars)
    {
        return false;
    }
    const auto link = source.link.trimmed();
    const QUrl url(link, QUrl::StrictMode);
    if (url.isValid() && !url.host().isEmpty() &&
        (url.scheme() == "http" || url.scheme() == "https"))
    {
        node.link = link;
    }
    node.collapsed = source.folded;
    node.parentId = parent;
    node.order = order;
    if (source.side == "left" || source.side == "right")
    {
        node.side = source.side;
    }
    for (const auto &value : source.tags)
    {
        const auto tag = value.trimmed();
        if (!tag.isEmpty() && node.tags.size() < Limits::kMaxTags && !node.tags.contains(tag) &&
            TitleRules::codePointCount(tag) <= Limits::kTagMaxChars)
        {
            node.tags.append(tag);
        }
    }
    node.contentRevision = 1;
    node.imagePng = images.take(source.image);
    if (!source.sourceId.isEmpty() && !importIds.contains(source.sourceId))
    {
        importIds.insert(source.sourceId, node.id);
    }
    sheet.nodes.insert(node.id, node);
    for (int i = 0; i < source.children.size(); ++i)
    {
        if (!addTopic(sheet, source.children.at(i), node.id, i, importIds, images))
        {
            return false;
        }
    }
    return true;
}

std::variant<Result, AppError> assemble(const QVector<Map> &maps, const QString &title,
                                        const XMindArchive *archive = nullptr)
{
    Result result;
    auto &project = result.project;
    project = ProjectFactory::createMindmap(title);
    project.sheets.clear();
    EmbeddedImages images(archive);
    for (int i = 0; i < maps.size(); ++i)
    {
        const auto &source = maps.at(i);
        const auto name = source.title.trimmed().isEmpty() ? QStringLiteral("工作表 %1").arg(i + 1)
                                                           : source.title.trimmed();
        auto created = ProjectFactory::createSheet("mindmap", name);
        auto sheet = std::get<TreeSheet>(std::get<Sheet>(std::move(created)));
        sheet.layout = source.layout.isEmpty() ? QStringLiteral("bilateral") : source.layout;
        QHash<QString, ID> ids;
        if (!addTopic(sheet, source.root, std::nullopt, 0, ids, images))
        {
            return AppError{"LIMIT_EXCEEDED", QStringLiteral("主题标题或备注过长，请精简后导入")};
        }
        for (const auto &edge : source.connections)
        {
            const auto from = ids.value(edge.source), to = ids.value(edge.target);
            if (!from.isEmpty() && !to.isEmpty() && from != to &&
                !TitleRules::exceedsTitleLimit(edge.text))
            {
                sheet.relations.append(
                    {newId(), from, to, edge.text, "end", sheet.defaultEdgeStyle});
            }
        }
        if (i == 0)
        {
            project.activeSheetId = sheet.id;
        }
        result.topics += static_cast<int>(sheet.nodes.size());
        project.sheets.append(std::move(sheet));
    }
    result.skippedImages = images.skipped;
    const auto errors = ProjectValidator::validate(project);
    if (!errors.isEmpty())
    {
        return errors.first();
    }
    const auto bytes = ProjectJson::toBytes(project);
    if (bytes.size() > Limits::kMaxFileBytes)
    {
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("转换结果超过灵芽工程的 10 MiB 上限")};
    }
    const auto checked = ProjectJson::fromBytes(bytes);
    if (const auto *error = std::get_if<AppError>(&checked))
    {
        return *error;
    }
    return result;
}
} // namespace

bool supports(const QString &path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == "xmind" || suffix == "opml" || suffix == "mm";
}

std::variant<Result, AppError> load(const QString &path)
{
    const QFileInfo source(path);
    if (!supports(path))
    {
        return AppError{"INVALID_ARGUMENT", QStringLiteral("请选择 XMind、OPML 或 FreeMind 文件")};
    }
    if (!source.isFile())
    {
        return AppError{"FILE_NOT_FOUND", QStringLiteral("导入文件不存在")};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return AppError{"FILE_READ_FAILED", QStringLiteral("无法读取导入文件")};
    }
    const auto title = TitleRules::normalize(source.completeBaseName());
    const auto suffix = source.suffix().toLower();
    if (suffix != "xmind")
    {
        const auto bytes = file.read(Limits::kMaxFileBytes + 1);
        if (bytes.size() > Limits::kMaxFileBytes)
        {
            return AppError{"LIMIT_EXCEEDED", QStringLiteral("导入文件超过 10 MiB，请先拆分")};
        }
        const auto maps = suffix == "opml" ? parseOpml(bytes, title) : parseFreeMind(bytes);
        if (const auto *error = std::get_if<AppError>(&maps))
        {
            return *error;
        }
        return assemble(std::get<QVector<Map>>(maps), title);
    }
    if (file.size() > kMaxArchiveBytes)
    {
        return AppError{"LIMIT_EXCEEDED", QStringLiteral("XMind 文件超过 64 MiB，请先拆分")};
    }
    XMindArchive archive(file);
    if (const auto error = archive.scan())
    {
        return *error;
    }
    const bool json = archive.contains("content.json");
    if (!json && !archive.contains("content.xml"))
    {
        return invalid(QStringLiteral("找不到 XMind 工作表内容"));
    }
    const auto input = archive.read(json ? "content.json" : "content.xml", Limits::kMaxFileBytes);
    if (const auto *error = std::get_if<AppError>(&input))
    {
        return *error;
    }
    const auto &bytes = std::get<QByteArray>(input);
    const auto maps = json ? parseXMindJson(bytes) : parseXMindXml(bytes);
    if (const auto *error = std::get_if<AppError>(&maps))
    {
        return *error;
    }
    return assemble(std::get<QVector<Map>>(maps), title, &archive);
}
} // namespace lingya::MindMapImport
