#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QGuiApplication>

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    return doctest::Context(argc, argv).run();
}
