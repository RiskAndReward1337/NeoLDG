#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

namespace neoldg {

enum class TunerModel {
    Ldg1000ProII,
    Ldg600ProII
};

struct MeterSample {
    quint16 forwardRaw = 0;
    quint16 reflectedRaw = 0;
    quint16 bandKey = 0;
};

struct DisplayTelemetry {
    double forwardWatts = 0.0;
    double reflectedWatts = 0.0;
    double swr = 1.0;
    QString bandName = QStringLiteral("Unknown");
    quint16 bandKey = 0;
};

enum class TuneOutcome {
    Idle,
    InProgress,
    Good,
    Okay,
    Fail,
    Error
};

enum class ResponseKind {
    ToggleAntenna,
    MemoryTune,
    FullTune,
    Bypass,
    AutoTune,
    ManualTune,
    Sync,
    Unknown
};

struct CommandResult {
    ResponseKind kind = ResponseKind::Unknown;
    QChar code;
    QString summary;
    bool success = false;
};

QString tunerModelDisplayName(TunerModel model);
QString tunerModelStorageKey(TunerModel model);
TunerModel tunerModelFromStorageKey(const QString& key);
double powerCalibrationForModel(TunerModel model);
double powerDisplayScaleForModel(TunerModel model);
bool isKnownBandKey(quint16 bandKey);
double wattsFromRaw(quint16 raw, double supplyVolts, TunerModel model);
double swrFromWatts(double forwardWatts, double reflectedWatts);
QString bandNameFromKey(quint16 bandKey);
DisplayTelemetry computeDisplayTelemetry(const MeterSample& sample, double supplyVolts, TunerModel model);
QString kindToLabel(ResponseKind kind);
CommandResult interpretCommandResponse(ResponseKind kind, QChar code);

} // namespace neoldg

Q_DECLARE_METATYPE(neoldg::TunerModel)
Q_DECLARE_METATYPE(neoldg::MeterSample)
Q_DECLARE_METATYPE(neoldg::DisplayTelemetry)
Q_DECLARE_METATYPE(neoldg::TuneOutcome)
Q_DECLARE_METATYPE(neoldg::CommandResult)
