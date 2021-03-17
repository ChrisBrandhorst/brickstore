/* Copyright (C) 2004-2021 Robert Griebl. All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU
** General Public License version 2 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/
#include <cstdio>
#include <cstring>

#include <QThreadPool>
#include <QStandardPaths>
#if defined(Q_OS_WINDOWS)
#  include <QMessageBox>
#  include <QApplication>
#else
#  include <QCoreApplication>
#endif
#if defined(Q_OS_UNIX)
#  include <sys/time.h>
#  include <sys/resource.h>
#endif

#include "utility.h"
#include "bricklink.h"
#include "rebuilddatabase.h"

#if !defined(BRICKSTORE_BACKEND_ONLY)
#  include "application.h"
#endif

#if defined(SENTRY_ENABLED)
#  include "sentry.h"
#  include "version.h"
#endif

// needed for themed common controls (e.g. file open dialogs)
#if defined(Q_CC_MSVC)
#  pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#if defined(STATIC_QT_BUILD) // this should be needed for linking against a static Qt, but it works without
#include <QtPlugin>
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qgif)
#endif

int main(int argc, char **argv)
{
    bool rebuild_db = false;
    bool skip_download = false;
    bool show_usage = false;

    if ((argc == 2) && (!strcmp(argv [1], "-h") || !strcmp(argv [1], "--help"))) {
        show_usage = true;
    }
    else if ((argc >= 2) && !strcmp(argv [1], "--rebuild-database")) {
        rebuild_db = true;
        if ((argc == 3) && !strcmp(argv[2], "--skip-download"))
            skip_download = true;
        else if (argc >= 3)
            show_usage = true;
    }

#if defined(BRICKSTORE_BACKEND_ONLY)
    if (!rebuild_db)
        show_usage = true;
#endif

#if defined(SANITIZER_ENABLED)
    QThreadPool::globalInstance()->setExpiryTimeout(0);
#endif

    int res = 0;

    if (show_usage) {
#if defined(Q_OS_WINDOWS)
        QApplication a(argc, argv);
        QMessageBox::information(nullptr, "Usage"_l1, QLatin1String(
                                     "<b>Usage:</b><br/>"
                                     "BrickStore.exe [&lt;files&gt;]<br/>"
                                     "BrickStore.exe --rebuild-database [--skip-download]<br/>"));
#else
        printf("Usage: %s [<files>]\n", argv[0]);
        printf("       %s --rebuild-database [--skip-download]\n", argv[0]);
#endif
        res = 1;
    } else if (rebuild_db) {
        QCoreApplication a(argc, argv);

        QString errstring;
        BrickLink::Core *bl = BrickLink::create(QStandardPaths::writableLocation(QStandardPaths::CacheLocation), &errstring);

        if (!bl) {
            fprintf(stderr, "Could not initialize the BrickLink kernel:\n%s\n", qPrintable(errstring));
            return 2;
        }

        RebuildDatabase rdb(skip_download);

        QMetaObject::invokeMethod(&a, [&rdb]() {
            exit(rdb.exec());
        }, Qt::QueuedConnection);

        res = a.exec();
    } else {
#if !defined(BRICKSTORE_BACKEND_ONLY)
#  if defined(Q_OS_UNIX)
        static constexpr rlim_t noFile = 30000;
        struct rlimit rlim;
        if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {

            if (rlim.rlim_cur < noFile) {
                rlim_t soft = (rlim.rlim_max == RLIM_INFINITY) ? noFile
                                                               : qMin(rlim.rlim_max, noFile);
                if (soft > rlim.rlim_cur) {
                    rlim.rlim_cur = soft;
                    setrlimit(RLIMIT_NOFILE, &rlim);
                }
            }
        }
        if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            qWarning() << "\nCould not retrieve the number of file that are allowed to be opened at the"
                          "same time. This may lead to image loading problems when opening huge files.\n";
        } else if (rlim.rlim_cur < noFile) {
            qWarning() << "\nYour system only allows for" << rlim.rlim_cur << "files to opened at the"
                          "same time. This may lead to image loading problems when opening huge files.\n";
        }
#  endif

        Application a(argc, argv);

#  if defined(SENTRY_ENABLED)
        sentry_options_t* options = sentry_options_new();
        sentry_options_set_dsn(options, "https://335761d80c3042548349ce5e25e12a06@o553736.ingest.sentry.io/5681421");

        const QByteArray appPath = QCoreApplication::applicationDirPath().toLocal8Bit();
        const QByteArray crashHandler = appPath % "/crashpad_handler"
#    if defined(Q_OS_WINDOWS)
                                        % ".exe"
#    endif
        ;
        sentry_options_set_handler_path(options, crashHandler.constData());
        sentry_options_set_database_path(options, appPath.constData());
        sentry_options_set_debug(options, 1);
        sentry_options_set_release(options, "brickstore@" BRICKSTORE_BUILD_NUMBER);
        if (sentry_init(options))
            qWarning() << "Could not initialize sentry.io!";
#  endif

        res = qApp->exec();

        // we are using QThreadStorage in thread-pool threads, so we have to make sure they are
        // all joined before destroying the static QThreadStorage objects.
        QThreadPool::globalInstance()->clear();
        QThreadPool::globalInstance()->waitForDone();

#  if defined(SENTRY_ENABLED)
        sentry_shutdown();
#  endif
#endif
    }

    return res;
}
