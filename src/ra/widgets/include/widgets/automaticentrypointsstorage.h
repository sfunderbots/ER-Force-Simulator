/***************************************************************************
 *   Copyright 2023 Paul Bergmann                                          *
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

#ifndef AUTOMATICENTRYPOINTSSTORAGE_H
#define AUTOMATICENTRYPOINTSSTORAGE_H

#include <QString>

struct AutomaticEntrypointsStorage {
    QString forGame;
    QString forBreak;
    QString forPostgame;

    bool operator==(const AutomaticEntrypointsStorage& rhs) const {
        return forGame == rhs.forGame
            && forBreak == rhs.forBreak
            && forPostgame == rhs.forPostgame;
    }

    bool allNull() const {
        return forGame.isNull()
            && forBreak.isNull()
            && forPostgame.isNull();
    }
};

#endif // AUTOMATICENTRYPOINTSSTORAGE_H
