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

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "protobuf/command.h"
#include "protobuf/status.h"
#include "protobuf/sslsim.h"
#include "protobuf/ssl_vision_detection_tracked.pb.h"
#include "protobuf/ssl_vision_wrapper_tracked.pb.h"
#include <QList>
#include <QMap>
#include <QPair>
#include <QQueue>
#include <QByteArray>
#include <tuple>
#include <random>

// higher values break the rolling friction of the ball
const float SIMULATOR_SCALE = 10.0f;
const float SUB_TIMESTEP = 1/200.f;
const float COLLISION_MARGIN = 0.04f;
const unsigned FOCAL_LENGTH = 390;

class QByteArray;
class QTimer;
class Timer;
class SSL_GeometryFieldSize;

namespace camun {
    namespace simulator {
        class SimRobot;
        class Simulator;
        class ErrorAggregator;
        struct SimulatorData;

        enum class ErrorSource {
            BLUE,
            YELLOW,
            CONFIG
        };
    }
}

class camun::simulator::Simulator : public QObject
{
    Q_OBJECT

public:
    typedef QMap<unsigned int, QPair<SimRobot*, unsigned int>> RobotMap; /*First int: ID, Second int: Generation*/

    explicit Simulator(const Timer *timer, const amun::SimulatorSetup &setup, bool useManualTrigger = false);
    ~Simulator() override;
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    void handleSimulatorTick(double timeStep);
    void seedPRGN(uint32_t seed);

signals:
    void gotPacket(const QByteArray &data, qint64 time, QString sender);
    void gotTrackedFrame(const QByteArray &data, qint64 time, QString sender);
    void sendStatus(const Status &status);
    void sendRadioResponses(const QList<robot::RadioResponse> &responses);
    void sendRealData(const QByteArray& data); // sends amun::SimulatorState
    void sendSSLSimError(const QList<SSLSimError>& errors, ErrorSource source);

public slots:
    void handleCommand(const Command &command);
    void handleRadioCommands(const SSLSimRobotControl& control, bool isBlue, qint64 processingStart);
    void setScaling(double scaling);
    void setFlipped(bool flipped);
    // checks for possible collisions with the robots on the target position of the ball
    // calls teleportRobotToFreePosition to move robots out of the way
    void safelyTeleportBall(const float x, const float y);
    void process();

private slots:
    void sendVisionPacket();

private:
    void sendSSLSimErrorInternal(ErrorSource source);
    void resetFlipped(RobotMap &robots, float side);
    std::tuple<QList<QByteArray>, QByteArray, qint64, QByteArray> createVisionPacket();
    void resetVisionPackets();
    void setTeam(RobotMap &list, float side, const robot::Team &team, QMap<uint32_t, robot::Specs>& specs);
    void moveBall(const sslsim::TeleportBall &ball);
    void moveRobot(const sslsim::TeleportRobot &robot);
    void teleportRobotToFreePosition(SimRobot *robot);
    void initializeDetection(SSL_DetectionFrame *detection, std::size_t cameraId);

private:
    template <typename T>
    std::vector<uint8_t> serializeProto(const T& msg) {
        size_t msg_size_bytes = msg.ByteSizeLong();
        std::vector<uint8_t> data(msg_size_bytes);
        if(data.data() != nullptr) {
            msg.SerializeToArray(data.data(), msg_size_bytes);
        }
        return data;
    }

    gameController::TrackerWrapperPacket getTrackerWrapperPacket();

private:
    typedef std::tuple<SSLSimRobotControl, qint64, bool> RadioCommand;
    SimulatorData *m_data;
    QQueue<RadioCommand> m_radioCommands;
    // The 4th element in the tuple is a serialize TrackedFrame containing
    // the 'true' state of the world
    QQueue<std::tuple<QList<QByteArray>, QByteArray, qint64, QByteArray>> m_visionPackets;
    QQueue<QTimer *> m_visionTimers;
    bool m_isPartial;
    const Timer *m_timer;
    QTimer *m_trigger;
    qint64 m_time;
    qint64 m_lastSentStatusTime;
    double m_timeScaling;
    bool m_enabled;
    bool m_charge;
    // systemDelay + visionProcessingTime = visionDelay
    qint64 m_visionDelay;
    qint64 m_visionProcessingTime;

    qint64 m_minRobotDetectionTime = 0;
    qint64 m_minBallDetectionTime = 0;
    qint64 m_lastBallSendTime = 0;
    std::map<qint64, unsigned> m_lastFrameNumber;
    ErrorAggregator *m_aggregator;

    std::mt19937 rand_shuffle_src = std::mt19937(std::random_device()());
};

#endif // SIMULATOR_H
