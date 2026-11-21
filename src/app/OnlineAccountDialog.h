#pragma once
#include <QDialog>
namespace lingya
{
class OnlineAuthService;
class OnlineAccountDialog : public QDialog
{
    Q_OBJECT
  public:
    enum class Entry
    {
        Account,
        ServiceSettings
    };
    explicit OnlineAccountDialog(OnlineAuthService *service, QWidget *parent = nullptr,
                                 Entry entry = Entry::Account, bool localSessionActive = false);
    void reject() override;

  private:
    OnlineAuthService *m_service;
};
} // namespace lingya
