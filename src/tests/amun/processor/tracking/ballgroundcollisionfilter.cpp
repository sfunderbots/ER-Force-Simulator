 
/***************************************************************************
 *   Copyright 2021 Andreas Wendler                                        *
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

#include "gtest/gtest.h"
#include "simulator/fastsimulator.h"
#include "simulator/simulator.h"
#include "core/configuration.h"
#include "core/coordinates.h"
#include "core/timer.h"
#include "core/vector.h"
#include "protobuf/geometry.h"
#include "protobuf/command.h"
#include "protobuf/status.h"
#include "visionlog/visionlogwriter.h"
#include "seshat/logfilewriter.h"
#include "tracking/tracker.h"

#include <iostream>
#include <map>
#include <QDebug>

// real ball pos and tracked ball pos
using TestFunction = std::function<void(Vector, std::optional<Vector>)>;
using TestFunctionWithRobot = std::function<void(Vector, std::optional<Vector>, std::vector<Vector>)>;

class SimulationController {
public:
    SimulationController();

    void saveToLog(const QString &filename);
    void simulate(float seconds);
    // this drive command stays active until a new one is given to the robot
    void driveRobot(bool isBlue, int id, Vector localVelocity, float angular, bool enableDribbler = false);
    // for simplicity, this test uses the perfect dribbler
    void setDribbler(bool isBlue, int id, bool enabled);
    void teleportRobot(bool isBlue, int id, Vector position, Vector lookDirection);
    void teleportBall(Vector position, Vector velocity);
    void addTestFunction(TestFunction f) { m_testFunctions.push_back(f); }
    void addTestFunction(TestFunctionWithRobot f) { m_robotTestFunctions.push_back(f); }
    void clearTestFunctions() { m_testFunctions.clear(); }

private:
    static amun::SimulatorSetup createDefaultSetup();
    void loadRobots(int blue, int yellow);

    void setBallPos(float x, float y);

private:
    Timer m_timer;
    camun::simulator::Simulator m_simulator;
    LogFileWriter m_logWriter;
    std::map<std::pair<bool, int>, SSLSimRobotControl> m_robotCommands;
    Tracker m_tracker;
    qint64 m_lastTrackingTime = 0;
    QList<QByteArray> m_simulatorTruth;
    std::vector<TestFunction> m_testFunctions;
    std::vector<TestFunctionWithRobot> m_robotTestFunctions;
    std::optional<Vector> m_lastTrueBallPos;
};

SimulationController::SimulationController() :
    m_simulator(&m_timer, createDefaultSetup(), true),
    m_tracker(false, false)
{
    m_timer.setScaling(0);
    m_timer.setTime(1234, 0);
    m_simulator.seedPRGN(14986);
    loadRobots(2, 0);

    Command c(new amun::Command);
    c->mutable_simulator()->set_enable(true);

    RealismConfigErForce realismConfig;
    loadConfiguration("simulator-realism/Realistic", &realismConfig, false);
    realismConfig.set_simulate_dribbling(false);
    // TODO: is this necessary?
    realismConfig.set_dribbler_ball_detections(0);
    c->mutable_simulator()->mutable_realism_config()->CopyFrom(realismConfig);
    m_simulator.handleCommand(c);

    m_simulator.connect(&m_simulator, &camun::simulator::Simulator::gotPacket, [this](const QByteArray &data, qint64 time, QString sender) {
        ASSERT_TRUE(sender == QString("simulator"));

        SSL_WrapperPacket wrapper;
        bool result = wrapper.ParseFromArray(data.data(), data.size());
        ASSERT_TRUE(result && "wrapper.ParseFromArray");

        // TODO: add radio commands to tracker
        m_tracker.queuePacket(data, time, sender);
        if (time - m_lastTrackingTime > 10000000) { // 10 ms
            // TODO: to better mimick the behavior of the real tracker, use a time offset here (and at the worldState query)
            m_tracker.process(time);
            m_tracker.finishProcessing();
            // TODO: use varying worldState times different from the process time
            // TODO: process more often (everey 10 ms as opposed to every 15)
            Status status = m_tracker.worldState(time, true);
            status->set_time(time);

            // TODO: this currently has an offset of one simulator output frame
            for(const QByteArray& data : m_simulatorTruth) {
                status->mutable_world_state()->add_reality()->ParseFromArray(data.data(), data.size());
            }
            m_simulatorTruth.clear();

            m_logWriter.writeStatus(status);
            m_lastTrackingTime = time;

            // extract ball/robot positions and check conditions
            std::optional<Vector> ballPos;
            if (status->has_world_state() && status->world_state().has_ball()) {
                ballPos = Vector(status->world_state().ball().p_x(), status->world_state().ball().p_y());
            }
            std::vector<Vector> robotPositions;
            if (status->has_world_state()) {
                for (const auto &r : status->world_state().blue()) {
                    robotPositions.push_back(Vector(r.p_x(), r.p_y()));
                }
                for (const auto &r : status->world_state().yellow()) {
                    robotPositions.push_back(Vector(r.p_x(), r.p_y()));
                }
            }
            if (m_lastTrueBallPos) {
                for (const auto &f : m_testFunctions) {
                    f(*m_lastTrueBallPos, ballPos);
                }
                for (const auto &f : m_robotTestFunctions) {
                    f(*m_lastTrueBallPos, ballPos, robotPositions);
                }
            }
        }
    });

    m_simulator.connect(&m_simulator, &camun::simulator::Simulator::sendRealData, [this](const QByteArray &data) {
        m_simulatorTruth.append(data);

        world::SimulatorState state;
        bool result = state.ParseFromArray(data.data(), data.size());
        ASSERT_TRUE(result && "wrapper.ParseFromArray");

        if (state.has_ball()) {
            m_lastTrueBallPos = Vector(state.ball().p_x(), state.ball().p_y());
        }
    });
}

amun::SimulatorSetup SimulationController::createDefaultSetup()
{
    amun::SimulatorSetup setup;
    loadConfiguration("simulator/2020", &setup, false);
    return setup;
}

void SimulationController::loadRobots(int blue, int yellow)
{
    // TODO: add robots to tracking (conditionally for different tests??)
    robot::Generation specs;
    loadConfiguration("robots/generation_2020", &specs, true);

    Command command(new amun::Command);
    for (int i = 0;i<blue;i++) {
        auto robot = command->mutable_set_team_blue()->add_robot();
        robot->CopyFrom(specs.default_());
        robot->set_id(i);
    }
    for (int i = 0;i<yellow;i++) {
        auto robot = command->mutable_set_team_yellow()->add_robot();
        robot->CopyFrom(specs.default_());
        robot->set_id(i);
    }

    m_simulator.handleCommand(command);
}

void SimulationController::driveRobot(bool isBlue, int id, Vector localVelocity, float angular, bool enableDribbler)
{
    SSLSimRobotControl control{new sslsim::RobotControl};

    auto* cmd = control->add_robot_commands();
    cmd->set_id(id);
    auto * localVel = cmd->mutable_move_command()->mutable_local_velocity();
    localVel->set_forward(localVelocity.x);
    localVel->set_left(localVelocity.y);
    localVel->set_angular(angular);
    cmd->set_dribbler_speed(enableDribbler ? 100 : 0);

    m_robotCommands[{isBlue, id}] = control;
}

void SimulationController::saveToLog(const QString &filename)
{
    m_logWriter.open(filename);
}

void SimulationController::simulate(float seconds)
{
    FastSimulator::goDeltaCallback(&m_simulator, &m_timer, seconds * 1e9, [this](){
        for (const auto &command : m_robotCommands) {
            m_simulator.handleRadioCommands(command.second, command.first.first, 0);
        }
    });
}

void SimulationController::teleportRobot(bool isBlue, int id, Vector position, Vector lookDirection)
{
    Command command(new amun::Command);
    auto teleport = command->mutable_simulator()->mutable_ssl_control()->add_teleport_robot();
    teleport->mutable_id()->set_id(id);
    teleport->mutable_id()->set_team(isBlue ? gameController::Team::BLUE : gameController::Team::YELLOW);
    coordinates::toVision(position, *teleport);
    teleport->set_v_x(0);
    teleport->set_v_y(0);
    teleport->set_v_angular(0);
    teleport->set_orientation(-lookDirection.normalized().angle());

    m_simulator.handleCommand(command);
}

void SimulationController::teleportBall(Vector position, Vector velocity)
{
    Command command(new amun::Command);
    auto teleport = command->mutable_simulator()->mutable_ssl_control()->mutable_teleport_ball();
    coordinates::toVision(position, *teleport);
    coordinates::toVisionVelocity(velocity, *teleport);

    m_simulator.handleCommand(command);
}

template<size_t maxDistanceMM>
static void testMaximumDistance(Vector truePos, std::optional<Vector> trackedPos)
{
    ASSERT_TRUE(trackedPos);
    const float dist = truePos.distance(*trackedPos);
    ASSERT_LE(dist, maxDistanceMM * 0.01f);
}

TEST(BallGroundCollisionFilter, BallPushing) {
    // pushes the ball, the ball becomes invisible, the tracked ball has to stay in front of the robot
    SimulationController s;
    s.teleportBall(Vector(-3.5, 1), Vector(0, 0));
    s.simulate(0.2);
    s.addTestFunction(testMaximumDistance<4>);
    s.teleportRobot(true, 0, Vector(-3, 1), Vector(-1, 0));
    s.driveRobot(true, 0, Vector(1, 0), 0);
    s.simulate(1.3);
}

TEST(BallGroundCollisionFilter, PushAndLeave) {
    // after pushing the ball and stopping, the ball should remain in the pushed
    // position even if it is still not visible and the robot drives backwards.
    SimulationController s;
    s.teleportBall(Vector(-3.5, 2), Vector(0, 0));
    s.simulate(0.2);
    s.addTestFunction(testMaximumDistance<5>);
    s.teleportRobot(true, 0, Vector(-3, 2), Vector(-1, 0));
    // push ball
    s.driveRobot(true, 0, Vector(1, 0), 0, true);
    s.simulate(1);
    // stop
    s.driveRobot(true, 0, Vector(0, 0), 0, true);
    s.simulate(0.5);
    // drive back without the ball
    s.driveRobot(true, 0, Vector(-1, 0), 0, false);
    s.simulate(1);
}

TEST(BallGroundCollisionFilter, PushingAndPulling) {
    // after pushing the ball and stopping, the ball should remain in the pushed
    // position even if it is still not visible and the robot drives backwards.
    // This should happen until the position the ball was pushed to (if it was) is visible
    // again, then the ball should snap to the pushing robot.
    // Therefore, this scenario dribbles the ball forwards, stops and then dribbles backwards.
    // During dribbling backwards, the tracked ball should deviate from the true ball for a while
    SimulationController s;
    s.teleportBall(Vector(-3.5, 2), Vector(0, 0));
    s.simulate(0.2);
    s.addTestFunction(testMaximumDistance<5>);
    s.teleportRobot(true, 0, Vector(-3, 2), Vector(-1, 0));
    // push ball
    s.driveRobot(true, 0, Vector(1, 0), 0, true);
    s.simulate(1);
    // stop
    s.driveRobot(true, 0, Vector(0, 0), 0, true);
    s.simulate(0.5);
    s.clearTestFunctions();
    // dribble the ball back
    s.driveRobot(true, 0, Vector(-1, 0), 0, true);
    float maxBallDistance = 0;
    s.addTestFunction([&maxBallDistance](Vector truePos, std::optional<Vector> trackedPos) {
        ASSERT_TRUE(trackedPos);
        const float dist = truePos.distance(*trackedPos);
        maxBallDistance = std::max(maxBallDistance, dist);
    });
    s.simulate(0.3);
    ASSERT_GE(maxBallDistance, 0.07);
    // after a short while, the ball should be snapped to the robot
    s.clearTestFunctions();
    s.addTestFunction(testMaximumDistance<5>);
    s.simulate(0.5);
}

TEST(BallGroundCollisionFilter, PushAndDuelBack) {
    // When the ball is pushed and the robot drives back again,
    // but the pushed position is obstructed by other robots,
    // the ball should not stay there.
    // This is mainly important for duels
    SimulationController s;
    s.teleportBall(Vector(-3.5, 2), Vector(0, 0));
    s.simulate(0.2);
    s.addTestFunction(testMaximumDistance<5>);
    s.teleportRobot(true, 0, Vector(-3, 2), Vector(-1, 0));
    s.teleportRobot(true, 1, Vector(-3.7, 2), Vector(1, 0));
    // push ball
    s.driveRobot(true, 0, Vector(1, 0), 0, true);
    s.simulate(0.5);
    // stop after having arrived in front of the second robot
    s.driveRobot(true, 0, Vector(0, 0), 0, true);
    s.simulate(0.3);
    // the second robot drives back, pushing the first one and the ball
    s.driveRobot(true, 0, Vector(0, 0), 0, false);
    s.driveRobot(true, 1, Vector(1, 0), 0, false);
    s.simulate(1);
}

static void testNotInRobot(Vector, std::optional<Vector> trackedPos, const std::vector<Vector> &robotPositions)
{
    if (!trackedPos) {
        return;
    }
    for (Vector v : robotPositions) {
        ASSERT_GE(trackedPos->distance(v), 0.06);
    }
}

TEST(BallGroundCollisionFilter, ShotAgainstRobot) {
    // when the ball is shot at a robot, the tracked ball should never visibly
    // enter the robot body and rather stay projected at the outside
    SimulationController s;
    s.addTestFunction(testNotInRobot);
    s.teleportRobot(true, 0, Vector(0, 3), Vector(0, -1));
    s.simulate(0.2);
    s.teleportBall(Vector(0, 0), Vector(0, 6));
    s.simulate(2);
}

template<size_t maxDistanceMM>
static void testNoBigJump(std::optional<Vector> *lastPos, Vector, std::optional<Vector> trackedPos) {
    ASSERT_TRUE(trackedPos);
    if (*lastPos) {
        const float dist = (*lastPos)->distance(*trackedPos);
        ASSERT_LE(dist, maxDistanceMM * 0.01f);
    }
    *lastPos = *trackedPos;
}

TEST(BallGroundCollisionFilter, DribbleAndRotate) {
    SimulationController s;
    s.teleportBall(Vector(-1.5, 3), Vector(0, 0));
    s.simulate(0.2);
    std::optional<Vector> lastPositionStore;
    TestFunction f = std::bind(testNoBigJump<10>, &lastPositionStore, std::placeholders::_1, std::placeholders::_2);
    s.addTestFunction(f);
    s.teleportRobot(true, 0, Vector(-1, 3), Vector(-1, 0));
    // push ball
    s.driveRobot(true, 0, Vector(1, 0), 0, true);
    s.simulate(0.8);
    // rotate
    s.driveRobot(true, 0, Vector(0, 0), 5, true);
    s.simulate(2);
}