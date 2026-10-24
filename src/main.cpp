#include <QApplication>
#include <QFile>
#include <QFont>

#include "ai/AiTypes.h"
#include "app/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LingyaMind");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("LingyaMind");

    // 中文无衬线字体，Windows优先微软雅黑（统一规范§14、原设计§9）
    QFont appFont(QStringLiteral("Microsoft YaHei"));
    appFont.setStyleHint(QFont::SansSerif);
    app.setFont(appFont);

    // 队列连接自定义类型注册（原设计§10.3）
    lingya::registerAiMetaTypes();

    // 浅色主题经qrc加载；QSS只负责控件样式，画布图元自绘（统一规范§14）
    QFile style(QStringLiteral(":/lingya/styles/light.qss"));
    if (style.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    lingya::MainWindow window;
    window.show();
    return app.exec();
}
