#include <vnm_qt_dispatch/vnm_qt_dispatch.h>

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    bool executed = false;

    const auto result = vnm::qt::post(
        &application,
        [&]()
        {
            executed = true;
            application.quit();
        });
    if (result != vnm::qt::Post_result::QUEUED) {
        std::fputs("The source-subproject consumer post was not queued.\n", stderr);
        return 1;
    }

    QTimer::singleShot(5000, &application, &QCoreApplication::quit);
    application.exec();

    if (!executed) {
        std::fputs("The source-subproject consumer post did not execute.\n", stderr);
        return 1;
    }

    return 0;
}
