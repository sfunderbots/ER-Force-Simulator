/**************************************************************************
*   Copyright 2021 Tobias Heineken                                        *
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
**************************************************************************/
import { RobotAccelerationProfile } from "base/trajectory";

/** Interface describing basic movement information of a team. */
export interface RobotSpecs {
	profile: RobotAccelerationProfile;
	vmax: number;
	vangular: number;
}

/** This map is meant to be filled with the measured RobotSpecs of opponent teams during tournaments. */
export let accelerationsByTeam: Map<string, RobotSpecs> = new Map();

