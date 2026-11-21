#pragma once

#include <QComboBox>
#include <optional>

namespace lingya::StyleChoices
{
QString colorName(const QString &color);
QString shapeName(const QString &shape);
void addShapes(QComboBox *box, bool flowOnly = false);

// 展示中文和色块，工程颜色值保存在选项数据中。
class ColorBox final : public QComboBox
{
  public:
    explicit ColorBox(QWidget *parent, const QString &automaticText = {});
    void setColorValue(const std::optional<QString> &color);
    std::optional<QString> colorValue() const;

  private:
    int m_automaticRow = -1;
    int m_pickerRow = -1;
    int m_customRow = -1;
    int m_previousRow = -1;
};
} // namespace lingya::StyleChoices
