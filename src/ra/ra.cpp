/***************************************************************************
 *   Copyright 2015 Michael Eischer, Philipp Nordhus                       *
 *   Robotics Erlangen e.V.                                                *
 *   http://www.robotics-erlangen.de/                                      *
 *   info@robotics-erlangen.de                                             *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "config/config.h"
#include "mainwindow.h"
#include <clocale>
#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QCommandLineParser>
#include <QDebug>
#include <QStyle>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Ra");
    app.setOrganizationName("ER-Force");
// available starting with Qt 5.1
#if (QT_VERSION >= QT_VERSION_CHECK(5, 1, 0))
    qApp->setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#ifdef Q_OS_OSX
    if (QDir::currentPath() == "/") {
        QDir::setCurrent(QDir::homePath());
    }
#endif

    std::setlocale(LC_NUMERIC, "C");

    QDir::addSearchPath("icon", QString(ERFORCE_DATADIR) + "/icons");

    QCommandLineParser parser;
    parser.setApplicationDescription("Ra");
    parser.addHelpOption();
    QCommandLineOption tournamentOption({"t", "game", "tournament"}, "Tournament mode");
    parser.addOption(tournamentOption);

    QCommandLineOption broadcastUiCommandsOption("broadcast-ui-commands", "Broadcasts the UI Commands to be used by external tools.");
    parser.addOption(broadcastUiCommandsOption);

    parser.process(app);

    MainWindow window(parser.isSet(tournamentOption), true, parser.isSet(broadcastUiCommandsOption));

    window.show();

    // qApp->setStyleSheet("");

    return app.exec();
}
