#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QQmlApplicationEngine engine;

    engine.load(QUrl(QStringLiteral(
        "qrc:/qt/qml/CanMonitor/Main.qml")));

    if(engine.rootObjects().isEmpty())
    {
        return -1;
    }

    return application.exec();
}
