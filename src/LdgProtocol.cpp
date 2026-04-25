#include "LdgProtocol.hpp"

#include <cmath>

namespace neoldg {

namespace {

struct BandRange {
    quint16 low;
    quint16 high;
    const char* name;
};

constexpr BandRange kBandRanges[] = {
    {8230, 9145, "160 m"},
    {4110, 4710, "80 m"},
    {3060, 3080, "60 m"},
    {2250, 2355, "40 m"},
    {1600, 1630, "30 m"},
    {1140, 1180, "20 m"},
    {900, 915, "17 m"},
    {766, 782, "15 m"},
    {655, 662, "12 m"},
    {550, 590, "10 m"},
    {320, 360, "6 m"},
};

QString summarizeTuneCode(QChar code)
{
    switch (code.unicode()) {
    case 'T':
        return QStringLiteral("Tune completed with a good match.");
    case 'M':
        return QStringLiteral("Tune completed with an acceptable match.");
    case 'F':
        return QStringLiteral("Tune failed to find a usable match.");
    case 'E':
        return QStringLiteral("The tuner returned an error.");
    default:
        return QStringLiteral("The tuner returned an unknown tune response.");
    }
}

} // namespace

QString tunerModelDisplayName(TunerModel model)
{
    switch (model) {
    case TunerModel::Ldg600ProII:
        return QStringLiteral("LDG-600ProII");
    case TunerModel::Ldg1000ProII:
    default:
        return QStringLiteral("LDG-1000ProII");
    }
}

QString tunerModelStorageKey(TunerModel model)
{
    switch (model) {
    case TunerModel::Ldg600ProII:
        return QStringLiteral("ldg-600-proii");
    case TunerModel::Ldg1000ProII:
    default:
        return QStringLiteral("ldg-1000-proii");
    }
}

TunerModel tunerModelFromStorageKey(const QString& key)
{
    if (key == QStringLiteral("ldg-600-proii")) {
        return TunerModel::Ldg600ProII;
    }

    return TunerModel::Ldg1000ProII;
}

double powerCalibrationForModel(TunerModel model)
{
    switch (model) {
    case TunerModel::Ldg600ProII:
        // Calibrated from hardware for the 600 model.
        // Example reference point: 96 W measured at raw forward 0x0159 with
        // the standard 13.8 V supply setting.
        return 674.25;
    case TunerModel::Ldg1000ProII:
    default:
        return 1000.0;
    }
}

double powerDisplayScaleForModel(TunerModel model)
{
    switch (model) {
    case TunerModel::Ldg600ProII:
        return 600.0;
    case TunerModel::Ldg1000ProII:
    default:
        return 1000.0;
    }
}

bool isKnownBandKey(quint16 bandKey)
{
    for (const auto& range : kBandRanges) {
        if (bandKey >= range.low && bandKey <= range.high) {
            return true;
        }
    }

    return false;
}

double wattsFromRaw(quint16 raw, double supplyVolts, TunerModel model)
{
    const double scaled = (powerCalibrationForModel(model) * supplyVolts * static_cast<double>(raw)) / (65536.0 * 0.707);
    return (scaled * scaled) / 50.0;
}

double swrFromWatts(double forwardWatts, double reflectedWatts)
{
    if (reflectedWatts <= 0.0 || forwardWatts <= reflectedWatts) {
        return 1.0;
    }

    const double ratio = std::sqrt(forwardWatts / reflectedWatts);
    if (std::abs(1.0 - ratio) < 0.0001) {
        return 1.0;
    }

    return std::abs((1.0 + ratio) / (1.0 - ratio));
}

QString bandNameFromKey(quint16 bandKey)
{
    for (const auto& range : kBandRanges) {
        if (bandKey >= range.low && bandKey <= range.high) {
            return QString::fromLatin1(range.name);
        }
    }

    return QStringLiteral("Unknown");
}

DisplayTelemetry computeDisplayTelemetry(const MeterSample& sample, double supplyVolts, TunerModel model)
{
    DisplayTelemetry telemetry;
    telemetry.forwardWatts = wattsFromRaw(sample.forwardRaw, supplyVolts, model);
    telemetry.reflectedWatts = wattsFromRaw(sample.reflectedRaw, supplyVolts, model);
    telemetry.swr = swrFromWatts(telemetry.forwardWatts, telemetry.reflectedWatts);
    telemetry.bandName = bandNameFromKey(sample.bandKey);
    telemetry.bandKey = sample.bandKey;
    return telemetry;
}

QString kindToLabel(ResponseKind kind)
{
    switch (kind) {
    case ResponseKind::ToggleAntenna:
        return QStringLiteral("antenna toggle");
    case ResponseKind::MemoryTune:
        return QStringLiteral("memory tune");
    case ResponseKind::FullTune:
        return QStringLiteral("full tune");
    case ResponseKind::Bypass:
        return QStringLiteral("bypass");
    case ResponseKind::AutoTune:
        return QStringLiteral("auto tune mode");
    case ResponseKind::ManualTune:
        return QStringLiteral("manual tune mode");
    case ResponseKind::Sync:
        return QStringLiteral("sync");
    case ResponseKind::Unknown:
    default:
        return QStringLiteral("command");
    }
}

CommandResult interpretCommandResponse(ResponseKind kind, QChar code)
{
    CommandResult result;
    result.kind = kind;
    result.code = code;

    switch (kind) {
    case ResponseKind::ToggleAntenna:
        result.success = code == QChar('1') || code == QChar('2') || code == QChar('A') || code == QChar('B');
        result.summary = result.success
            ? QStringLiteral("Active antenna changed to %1.").arg(code)
            : QStringLiteral("Unexpected antenna response: %1").arg(code);
        break;
    case ResponseKind::MemoryTune:
    case ResponseKind::FullTune:
        result.success = code == QChar('T') || code == QChar('M');
        result.summary = summarizeTuneCode(code);
        break;
    case ResponseKind::Bypass:
        result.success = code == QChar('P');
        result.summary = result.success
            ? QStringLiteral("Bypass mode enabled.")
            : QStringLiteral("Unexpected bypass response: %1").arg(code);
        break;
    case ResponseKind::AutoTune:
        result.success = code == QChar('A');
        result.summary = result.success
            ? QStringLiteral("Automatic tuning mode enabled.")
            : QStringLiteral("Unexpected auto-mode response: %1").arg(code);
        break;
    case ResponseKind::ManualTune:
        result.success = code == QChar('M');
        result.summary = result.success
            ? QStringLiteral("Manual tuning mode enabled.")
            : QStringLiteral("Unexpected manual-mode response: %1").arg(code);
        break;
    case ResponseKind::Sync:
        result.success = !code.isNull();
        result.summary = QStringLiteral("Sync response received: %1").arg(code);
        break;
    case ResponseKind::Unknown:
    default:
        result.success = !code.isNull();
        result.summary = QStringLiteral("Response received: %1").arg(code);
        break;
    }

    if (code == QChar('E') && (kind == ResponseKind::MemoryTune || kind == ResponseKind::FullTune)) {
        result.success = false;
    }

    return result;
}

} // namespace neoldg
