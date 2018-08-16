/***************************************************************************
 *   Copyright 2018 Andreas Wendler                                        *
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

#include "typescript.h"

#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <vector>

#include "v8.h"
#include "libplatform/libplatform.h"
#include "js_amun.h"
#include "js_path.h"

using namespace v8;

Typescript::Typescript(const Timer *timer, StrategyType type, bool debugEnabled, bool refboxControlEnabled) :
    AbstractStrategyScript (timer, type, debugEnabled, refboxControlEnabled)
{
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    m_isolate = Isolate::New(create_params);
    m_isolate->SetRAILMode(PERFORMANCE_LOAD);
    m_isolate->Enter();

    HandleScope handleScope(m_isolate);
    Local<ObjectTemplate> globalTemplate = ObjectTemplate::New(m_isolate);
    registerDefineFunction(globalTemplate);
    Local<Context> context = Context::New(m_isolate, nullptr, globalTemplate);
    Context::Scope contextScope(context);
    Local<Object> global = context->Global();
    registerAmunJsCallbacks(m_isolate, global, this);
    registerPathJsCallbacks(m_isolate, global, this);
    m_context.Reset(m_isolate, context);
}

Typescript::~Typescript()
{
    // TODO: delete objects
    //m_context.Reset();
    //m_isolate->Dispose();
}

bool Typescript::canHandle(const QString &filename)
{
    QFileInfo file(filename);
    // TODO: js is only for temporary tests
    return file.fileName().split(".").last() == "js";
}

AbstractStrategyScript* Typescript::createStrategy(const Timer *timer, StrategyType type, bool debugEnabled, bool refboxControlEnabled)
{
    return new Typescript(timer, type, debugEnabled, refboxControlEnabled);
}

bool Typescript::loadScript(const QString &filename, const QString &entryPoint, const world::Geometry &geometry, const robot::Team &team)
{
    // TODO: factor this common code to a function in AbstractStrategyScript
    Q_ASSERT(m_filename.isNull());

    // startup strategy information
    m_filename = filename;
    m_name = "<no script>";
    // strategy modules are loaded relative to the init script
    m_baseDir = QFileInfo(m_filename).absoluteDir();

    m_geometry.CopyFrom(geometry);
    m_team.CopyFrom(team);
    takeDebugStatus();

    QFile file(filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        QByteArray contentBytes = content.toLatin1();

        HandleScope handleScope(m_isolate);
        Local<Context> context = Local<Context>::New(m_isolate, m_context);
        Context::Scope contextScope(context);

        Local<String> source = String::NewFromUtf8(m_isolate,
                                            contentBytes.data(), NewStringType::kNormal).ToLocalChecked();

        // Compile the source code.
        Local<Script> script;
        TryCatch tryCatch(m_isolate);
        if (!Script::Compile(context, source).ToLocal(&script)) {
            String::Utf8Value error(m_isolate, tryCatch.StackTrace(context).ToLocalChecked());
            m_errorMsg = "<font color=\"red\">" + QString(*error) + "</font>";
            return false;
        }

        // execute the script once to get entrypoints etc.
        m_currentExecutingModule = m_filename;
        script->Run(context);
        if (tryCatch.HasTerminated() || tryCatch.HasCaught()) {
            String::Utf8Value error(m_isolate, tryCatch.Exception());
            m_errorMsg = "<font color=\"red\">" + QString(*error) + "</font>";
            return false;
        }
        Local<Object> initExport = Local<Value>::New(m_isolate, *m_requireCache[m_filename])->ToObject(context).ToLocalChecked();
        Local<String> scriptInfoString = String::NewFromUtf8(m_isolate, "scriptInfo", NewStringType::kNormal).ToLocalChecked();
        if (!initExport->Has(context, scriptInfoString).ToChecked()) {
            // the script returns nothing
            m_errorMsg = "<font color=\"red\">Script must export scriptInfo object!</font>";
            return false;
        }
        Local<Value> result = initExport->Get(context, scriptInfoString).ToLocalChecked();

        if (!result->IsObject()) {
            m_errorMsg = "<font color=\"red\">scriptInfo export must be an object!</font>";
            return false;
        }

        Local<Object> resultObject = result->ToObject(context).ToLocalChecked();
        Local<String> nameString = String::NewFromUtf8(m_isolate, "name", NewStringType::kNormal).ToLocalChecked();
        Local<String> entrypointsString = String::NewFromUtf8(m_isolate, "entrypoints", NewStringType::kNormal).ToLocalChecked();
        if (!resultObject->Has(nameString) || !resultObject->Has(entrypointsString)) {
            m_errorMsg = "<font color=\"red\">scriptInfo export must be an object containing 'name' and 'entrypoints'!</font>";
            return false;
        }

        // TODO: Has will be deprecated
        // TODO: Get will be deprecated
        Local<Value> maybeName = resultObject->Get(nameString);
        if (!maybeName->IsString()) {
            m_errorMsg = "<font color=\"red\">Script name must be a string!</font>";
            return false;
        }
        Local<String> name = maybeName->ToString(context).ToLocalChecked();
        m_name = QString(*String::Utf8Value(name));
        // TODO: get options from strategy

        Local<Value> maybeEntryPoints = resultObject->Get(entrypointsString);
        if (!maybeEntryPoints->IsObject()) {
            m_errorMsg = "<font color=\"red\">Entrypoints must be an object!</font>";
            return false;
        }

        m_entryPoints.clear();
        QMap<QString, Local<Function>> entryPoints;
        Local<Object> entrypointsObject = maybeEntryPoints->ToObject(context).ToLocalChecked();
        Local<Array> properties = entrypointsObject->GetOwnPropertyNames();
        for (unsigned int i = 0;i<properties->Length();i++) {
            Local<Value> key = properties->Get(i);
            Local<Value> value = entrypointsObject->Get(key);
            if (!value->IsFunction()) {
                m_errorMsg = "<font color=\"red\">Entrypoints must contain functions!</font>";
                return false;
            }
            Local<Function> function = Local<Function>::Cast(value);

            QString keyString(*String::Utf8Value(key));
            m_entryPoints.append(keyString);
            entryPoints[keyString] = function;
        }

        if (!chooseEntryPoint(entryPoint)) {
            return false;
        }

        m_function.Reset(m_isolate, entryPoints[m_entryPoint]);
        return true;
    } else {
        m_errorMsg = "<font color=\"red\">Could not open file " + filename + "</font>";
        return false;
    }
}

void Typescript::defineModule(const FunctionCallbackInfo<Value> &args)
{
    Typescript *t = static_cast<Typescript*>(Local<External>::Cast(args.Data())->Value());
    Isolate *isolate = args.GetIsolate();
    Local<Array> imports = Local<Array>::Cast(args[0]);
    Local<Function> module = Local<Function>::Cast(args[1]);

    std::vector<Local<Value>> parameters;

    Local<FunctionTemplate> requireTemplate = Local<FunctionTemplate>::New(isolate, t->m_requireTemplate);
    Local<Context> context = isolate->GetCurrentContext();
    parameters.push_back(requireTemplate->GetFunction(context).ToLocalChecked());

    Local<Object> exports = Object::New(isolate);
    parameters.push_back(exports);
    t->m_requireCache[t->m_currentExecutingModule] = new Global<Value>(isolate, exports);


    for (unsigned int i = 2;i<imports->Length();i++) {
        QString name = *String::Utf8Value(imports->Get(context, i).ToLocalChecked());
        if (!t->loadModule(name)) {
            return;
        }
        Local<Value> mod = Local<Value>::New(isolate, *t->m_requireCache[name]);
        parameters.push_back(mod);
    }

    TryCatch tryCatch(isolate);
    module->Call(context, context->Global(), parameters.size(), parameters.data());
    if (tryCatch.HasCaught() || tryCatch.HasTerminated()) {
        tryCatch.ReThrow();
        return;
    }
}

void Typescript::registerDefineFunction(Local<ObjectTemplate> global)
{
    Local<String> name = String::NewFromUtf8(m_isolate, "define", NewStringType::kNormal).ToLocalChecked();
    global->Set(name, FunctionTemplate::New(m_isolate, defineModule, External::New(m_isolate, this)));

    Local<FunctionTemplate> requireTemplate = FunctionTemplate::New(m_isolate, performRequire, External::New(m_isolate, this));
    m_requireTemplate.Reset(m_isolate, requireTemplate);
}

bool Typescript::loadModule(QString name)
{
    if (!m_requireCache.contains(name)) {
        QFileInfo initInfo(m_filename);
        QFile file(initInfo.absolutePath() + "/" + name + ".js");
        file.open(QIODevice::ReadOnly);
        QTextStream in(&file);
        QString content = in.readAll();
        QByteArray contentBytes = content.toLatin1();

        Local<String> source = String::NewFromUtf8(m_isolate,
                                            contentBytes.data(), NewStringType::kNormal).ToLocalChecked();
        Local<Context> context = m_isolate->GetCurrentContext();

        // Compile the source code.
        Local<Script> script;
        TryCatch tryCatch(m_isolate);
        if (!Script::Compile(context, source).ToLocal(&script)) {
            tryCatch.ReThrow();
            return false;
        }

        // execute the script once to get entrypoints etc.
        QString moduleBefore = m_currentExecutingModule;
        m_currentExecutingModule = name;
        script->Run(context);
        if (tryCatch.HasCaught() || tryCatch.HasTerminated()) {
            qDebug() <<"Run exception!";
            tryCatch.ReThrow();
            return false;
        }
        m_currentExecutingModule = moduleBefore;
    }
    return true;
}

void Typescript::performRequire(const FunctionCallbackInfo<Value> &args)
{
    Typescript *t = static_cast<Typescript*>(Local<External>::Cast(args.Data())->Value());
    QString name = *String::Utf8Value(args[0]);
    t->loadModule(name);
    args.GetReturnValue().Set(*t->m_requireCache[name]);
}

void Typescript::addPathTime(double time)
{
    m_totalPathTime += time;
}

bool Typescript::process(double &pathPlanning, const world::State &worldState, const amun::GameState &refereeState, const amun::UserInput &userInput)
{
    Q_ASSERT(!m_entryPoint.isNull());

    m_worldState.CopyFrom(worldState);
    m_worldState.clear_vision_frames();
    m_refereeState.CopyFrom(refereeState);
    m_userInput.CopyFrom(userInput);
    takeDebugStatus();

    // TODO: script timeout
    m_totalPathTime = 0;

    HandleScope handleScope(m_isolate);
    Local<Context> context = Local<Context>::New(m_isolate, m_context);
    Context::Scope contextScope(context);

    TryCatch tryCatch(m_isolate);
    Local<Function> function = Local<Function>::New(m_isolate, m_function);
    function->Call(context, context->Global(), 0, nullptr);
    if (tryCatch.HasTerminated() || tryCatch.HasCaught()) {
        String::Utf8Value error(m_isolate, tryCatch.StackTrace(context).ToLocalChecked());
        m_errorMsg = "<font color=\"red\">" + QString(*error) + "</font>";
        return false;
    }
    pathPlanning = m_totalPathTime;
    return true;
}