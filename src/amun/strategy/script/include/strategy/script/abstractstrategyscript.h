/***************************************************************************
 *   Copyright 2015 Michael Eischer, Paul Bergmann                         *
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

#ifndef ABSTRACTSTRATEGYSCRIPT_H
#define ABSTRACTSTRATEGYSCRIPT_H

#include "gamecontroller/gamecontrollerconnection.h"
#include "protobuf/command.h"
#include "protobuf/debug.pb.h"
#include "protobuf/gamestate.pb.h"
#include "protobuf/robot.pb.h"
#include "protobuf/robotcommand.h"
#include "protobuf/status.h"
#include "protobuf/userinput.pb.h"
#include "protobuf/world.pb.h"
#include "strategytype.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QList>

class DebugHelper;
class Timer;

class CompilerRegistry;

class AbstractStrategyScript : public QObject
{
    Q_OBJECT
public:
    AbstractStrategyScript(const Timer *timer, StrategyType type, bool refboxControlEnabled, CompilerRegistry* registry = nullptr);
    ~AbstractStrategyScript() override;
    AbstractStrategyScript(const AbstractStrategyScript&) = delete;
    AbstractStrategyScript& operator=(const AbstractStrategyScript&) = delete;

    // simple factory to allow for different strategy handlers
    static bool canHandle(const QString &filename);

    // errors are reported via changeLoadState. If an error occured, the error msg can be retrieved via errorMsg()
    void loadScript(const QString &filename, const QString &entryPoint, const world::Geometry &geometry, const robot::Team &team);
    // must only be called after loadScript was executed successfully
    bool process(double &pathPlanning, const world::State &worldState, const amun::GameState &refereeState, const amun::UserInput &userInput);
    virtual bool triggerDebugger();
    virtual void startProfiling() {}
    virtual void endProfiling(const std::string &filename) {}
    virtual bool canReloadInPlace() const { return false; }
    virtual bool canHandleDynamic(const QString &filename) const = 0;

    void setSelectedOptions(const QStringList &options);
    void setDebugHelper(DebugHelper *helper);
    void setIsInternalAutoref(bool internal) { m_isInternalAutoref = internal; }
    void setIsPerformanceMode(bool performance) { m_isPerformanceMode = performance; }
    void setIsReplay(bool replay) { m_isReplay = replay; }
    void setFlipped(bool flipped) { m_isFlipped = flipped; }
    void setTournamentMode(bool isTournament) { m_isTournamentMode = isTournament; }
    void setDebug(bool enableDebug) { m_debugEnabled = enableDebug; }
    void setCurrentStatus(const Status &status) { m_currentStatus = status; }
    const Status &getCurrentStatus() const { return m_currentStatus; }

    void setGameControllerConnection(std::shared_ptr<GameControllerConnection> &connection);
    std::shared_ptr<GameControllerConnection> getGameControllerConnection() const { return m_gameControllerConnection; }

    // getter functions
    QString errorMsg() const { return m_errorMsg; }
    // may be called after loadScript / process
    // has to be called before calling log, addDebug, addPlot, ...
    // as this function inserts the pointer dV into this object and log uses that pointer.
    // The status containing dV has to be managed outside of AbstractStrategyScript,
    // because it should be possible to have multiple debugValues in one Status,
    // containing information of different scripts at once.
    amun::DebugValues* setDebugValues(amun::DebugValues* dV);
    void clearDebugValues(){ if(m_debugValues) m_debugValues->Clear();}

    QStringList entryPoints() const { return m_entryPoints; }
    QString entryPoint() const { return m_entryPoint; }
    QString name() const { return m_name; }
    QStringList options() const { return m_options; }
    QStringList selectedOptions() const { return m_selectedOptions; }
    bool hasDebugger() const { return m_hasDebugger; }
    bool isInternalAutoref() const { return m_isInternalAutoref; }
    bool isPerformanceMode() const { return m_isPerformanceMode; }
    bool isReplay() const { return m_isReplay;}
    void addRefereeReply(SSL_RefereeRemoteControlReply reply) { m_refereeReplies.append(reply); }
    SSL_RefereeRemoteControlReply nextRefereeReply() { return m_refereeReplies.takeFirst(); }
    bool hasRefereeReply() const { return m_refereeReplies.size() > 0; }
    bool isFlipped() const { return m_isFlipped; }
    bool isTournamentMode() const { return m_isTournamentMode; }
    bool isDebug() const { return m_debugEnabled; }

    qint64 time() const;

    void log(const QString &text);
    amun::Visualization *addVisualization();
    void removeVisualizations();
    amun::DebugValue *addDebug();
    amun::PlotValue *addPlot();
    amun::RobotValue *addRobotValue();
    bool refboxControlEnabled() const { return m_refboxControlEnabled; }
    void setCommands(const QList<RobotCommandInfo> &commands);
    bool sendCommand(const Command &command);
    bool sendNetworkReferee(const QByteArray &referee);
    void sendMixedTeam(const QByteArray &info);
    const world::Geometry& geometry() const { return m_geometry; }
    const robot::Team& team() const { return m_team; }
    const world::State& worldState() const { return m_worldState; }
    const amun::GameState& refereeState() const { return m_refereeState; }
    const amun::UserInput& userInput() const { return m_userInput; }
    bool isBlue() const { return m_type == StrategyType::BLUE; }
    StrategyType getStrategyType() const { return m_type; }
    QDir baseDir() const { return m_baseDir; }

signals:
    // wrapper may listen to reload request, but doesn't have to
    void requestReload();
    void gotCommand(const Command &command);
    void sendStrategyCommands(bool blue, const QList<RobotCommandInfo> &commands, qint64 time);
    void sendMixedTeamInfo(const QByteArray &data);
    void sendNetworkRefereeCommand(const QByteArray &data);
    void changeLoadState(amun::StatusStrategy::STATE state);

protected:
    bool chooseEntryPoint(QString entryPoint);
    virtual void loadScript(const QString &filename, const QString &entryPoint) = 0;
    virtual bool process(double &pathPlanning) = 0;

protected:
    QStringList m_entryPoints;
    QString m_entryPoint;
    QString m_name;
    QStringList m_options;
    QStringList m_selectedOptions;

    const Timer *m_timer;
    const StrategyType m_type;
    const bool m_refboxControlEnabled;

    QString m_errorMsg;
    bool m_hasDebugger;
    DebugHelper *m_debugHelper;
    bool m_isInternalAutoref;
    bool m_isPerformanceMode;
    bool m_isReplay;
    bool m_isFlipped;
    bool m_isTournamentMode;
    bool m_debugEnabled;
    QDir m_baseDir;
    QString m_filename;

    world::Geometry m_geometry;
    robot::Team m_team;
    world::State m_worldState;
    amun::GameState m_refereeState;
    amun::UserInput m_userInput;
    Status m_currentStatus; // used for replay tests

    QList<SSL_RefereeRemoteControlReply> m_refereeReplies;

    std::shared_ptr<GameControllerConnection> m_gameControllerConnection;

    CompilerRegistry* m_compilerRegistry;
private:
    amun::DebugValues* m_debugValues = nullptr;
};

#endif // ABSTRACTSTRATEGYSCRIPT_H