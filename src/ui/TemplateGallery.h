#pragma once
#include <QDialog>
#include <QIcon>
namespace lingya
{
struct Project;

class TemplateGallery : public QDialog
{
  public:
    explicit TemplateGallery(bool allowStructure, QWidget *parent = nullptr);
    QString templateId, layoutCode;
    static QIcon preview(const QString &id);
    static QIcon preview(const Project &project);
};
} // namespace lingya
