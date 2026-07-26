#include <vnm_qt_dispatch/vnm_qt_dispatch.h>

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    const int result = vnm::qt::call(
        &application,
        []()
        {
            return 73;
        });
    if (result != 73) {
        std::fputs("The installed consumer call returned the wrong value.\n", stderr);
        return 1;
    }

    return 0;
}
