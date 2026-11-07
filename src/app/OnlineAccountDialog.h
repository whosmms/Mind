#pragma once
#include <QDialog>
namespace lingya
{
class OnlineAuthService;
class OnlineAccountDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit OnlineAccountDialog(OnlineAuthService *service, QWidget *parent = nullptr);
    void reject() override;

  private:
    OnlineAuthService *m_service;
};
} // namespace lingya
