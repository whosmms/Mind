#pragma once
#include <QVector>
#include <QWidget>
class QGridLayout;
class QListWidget;
class QLabel;
class QPushButton;
class QResizeEvent;

namespace lingya
{
// 首页只呈现本机最近工程和模板意图；文件保护、账号权限与加载仍由宿主处理。
class HomePage final : public QWidget
{
    Q_OBJECT
  public:
    explicit HomePage(QWidget *parent = nullptr);
    void setRecentFiles(const QVector<QString> &paths);
    void setCurrentTitle(const QString &title);
  signals:
    void templateRequested(QString kind);
    void templateGalleryRequested();
    void openRequested();
    void recentRequested(QString path);
    void removeRecentRequested(QString path);
    void continueRequested();
    void preferencesRequested();

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void arrangeCards();
    QGridLayout *m_cardLayout = nullptr;
    QVector<QPushButton *> m_cards;
    QListWidget *m_recents = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_current = nullptr;
    int m_columns = 0;
};
} // namespace lingya
