#include "LdgTunerController.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QTimer>

namespace {

constexpr int kCommandSettleMs = 60;
constexpr int kResponseTimeoutMs = 2200;
constexpr int kReturnToMeterDelayMs = 100;
constexpr int kSilentReconnectDelayMs = 200;
constexpr int kMaxSilentReconnectAttempts = 3;

} // namespace

LdgTunerController::LdgTunerController(QObject* parent)
    : QObject(parent)
{
    serial_.setBaudRate(QSerialPort::Baud38400);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    controlSettleTimer_.setSingleShot(true);
    responseTimer_.setSingleShot(true);
    silentReconnectTimer_.setSingleShot(true);

    connect(&serial_, &QSerialPort::readyRead, this, &LdgTunerController::onReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred, this, &LdgTunerController::onErrorOccurred);
    connect(&controlSettleTimer_, &QTimer::timeout, this, &LdgTunerController::onControlSettled);
    connect(&responseTimer_, &QTimer::timeout, this, &LdgTunerController::onResponseTimeout);
    connect(&silentReconnectTimer_, &QTimer::timeout, this, &LdgTunerController::onSilentReconnectTimeout);
}

LdgTunerController::~LdgTunerController()
{
    disconnectSerial();
}

bool LdgTunerController::isConnected() const
{
    return serial_.isOpen() || silentReconnectInProgress_;
}

bool LdgTunerController::isBusy() const
{
    return busy_;
}

QString LdgTunerController::portName() const
{
    return serial_.portName();
}

void LdgTunerController::connectSerial(const QString& portName)
{
    silentReconnectTimer_.stop();
    silentReconnectInProgress_ = false;
    silentReconnectAttempts_ = 0;
    reconnectPortName_.clear();

    if (serial_.isOpen()) {
        disconnectSerial();
    }

    openSerialPort(portName, true, true, true);
}

void LdgTunerController::disconnectSerial()
{
    const bool wasRecovering = silentReconnectInProgress_;
    const QString previousPort = wasRecovering ? reconnectPortName_ : serial_.portName();

    controlSettleTimer_.stop();
    responseTimer_.stop();
    silentReconnectTimer_.stop();
    setBusy(false);
    resetPendingCommand();
    resetMeterParser();
    rxMode_ = RxMode::Meter;
    reconnectPortName_.clear();
    silentReconnectInProgress_ = false;
    silentReconnectAttempts_ = 0;

    if (serial_.isOpen()) {
        serial_.close();
        emitStatus(QStringLiteral("Disconnected from %1.").arg(previousPort));
        emit connectionChanged(false, QString());
    } else if (wasRecovering && !previousPort.isEmpty()) {
        emitStatus(QStringLiteral("Disconnected from %1.").arg(previousPort));
        emit connectionChanged(false, QString());
    }

    emit tuneOutcomeChanged(neoldg::TuneOutcome::Idle, QStringLiteral("Idle"));
}

void LdgTunerController::toggleAntenna()
{
    startCommand(neoldg::ResponseKind::ToggleAntenna, 'A', QStringLiteral("toggle antenna"));
}

void LdgTunerController::memoryTune()
{
    startCommand(neoldg::ResponseKind::MemoryTune, 'T', QStringLiteral("memory tune"));
}

void LdgTunerController::fullTune()
{
    startCommand(neoldg::ResponseKind::FullTune, 'F', QStringLiteral("full tune"));
}

void LdgTunerController::toggleBypass()
{
    if (bypassActive_) {
        startCommand(neoldg::ResponseKind::AutoTune, 'C', QStringLiteral("return to auto tune mode"));
    } else {
        startCommand(neoldg::ResponseKind::Bypass, 'P', QStringLiteral("enable bypass"));
    }
}

void LdgTunerController::setAutoTuneMode()
{
    startCommand(neoldg::ResponseKind::AutoTune, 'C', QStringLiteral("enable auto tune mode"));
}

void LdgTunerController::setManualTuneMode()
{
    startCommand(neoldg::ResponseKind::ManualTune, 'M', QStringLiteral("enable manual tune mode"));
}

void LdgTunerController::onReadyRead()
{
    const QByteArray data = serial_.readAll();
    if (data.isEmpty()) {
        return;
    }

    if (rxMode_ == RxMode::ControlSettling) {
        return;
    }

    if (rxMode_ == RxMode::AwaitingResponse) {
        for (const char byte : data) {
            if (static_cast<unsigned char>(byte) >= 0x30U) {
                responseTimer_.stop();
                const auto result = neoldg::interpretCommandResponse(pendingKind_, QChar::fromLatin1(byte));
                finishCommand(result);
                return;
            }
        }
        return;
    }

    for (const char byte : data) {
        processMeterByte(byte);
    }
}

void LdgTunerController::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }

    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError || error == QSerialPort::DeviceNotFoundError) {
        emitStatus(QStringLiteral("Serial error: %1").arg(serial_.errorString()), true);
        disconnectSerial();
    }
}

void LdgTunerController::onControlSettled()
{
    if (!serial_.isOpen()) {
        return;
    }

    serial_.clear(QSerialPort::Input);
    resetMeterParser();
    rxMode_ = RxMode::AwaitingResponse;
    sendWakeAndCommand(pendingCommand_);
    responseTimer_.start(kResponseTimeoutMs);
    emit logMessage(QStringLiteral("Sent %1 command.").arg(pendingDescription_));
}

void LdgTunerController::onResponseTimeout()
{
    emitStatus(QStringLiteral("Timed out waiting for the %1 response.").arg(pendingDescription_), true);

    neoldg::CommandResult result;
    result.kind = pendingKind_;
    result.code = QChar('E');
    result.success = false;
    result.summary = QStringLiteral("Timed out waiting for the tuner response.");
    finishCommand(result);
}

void LdgTunerController::onSilentReconnectTimeout()
{
    if (!silentReconnectInProgress_) {
        return;
    }

    if (reconnectPortName_.isEmpty()) {
        silentReconnectInProgress_ = false;
        silentReconnectAttempts_ = 0;
        emit connectionChanged(false, QString());
        emit tuneOutcomeChanged(neoldg::TuneOutcome::Idle, QStringLiteral("Idle"));
        return;
    }

    if (openSerialPort(reconnectPortName_, false, false, false)) {
        silentReconnectInProgress_ = false;
        silentReconnectAttempts_ = 0;
        reconnectPortName_.clear();
        return;
    }

    ++silentReconnectAttempts_;
    if (silentReconnectAttempts_ < kMaxSilentReconnectAttempts) {
        silentReconnectTimer_.start(kSilentReconnectDelayMs);
        return;
    }

    const QString failedPort = reconnectPortName_;
    reconnectPortName_.clear();
    silentReconnectInProgress_ = false;
    silentReconnectAttempts_ = 0;
    emitStatus(QStringLiteral("Lost meter stream synchronization and could not reopen %1: %2")
        .arg(failedPort, serial_.errorString()), true);
    emit connectionChanged(false, QString());
    emit tuneOutcomeChanged(neoldg::TuneOutcome::Idle, QStringLiteral("Idle"));
}

bool LdgTunerController::openSerialPort(
    const QString& portName,
    bool announceConnection,
    bool announceFailure,
    bool emitConnectionSignal)
{
    serial_.setPortName(portName);
    if (!serial_.open(QIODevice::ReadWrite)) {
        if (announceFailure) {
            emitStatus(QStringLiteral("Could not open %1: %2").arg(portName, serial_.errorString()), true);
        }
        if (emitConnectionSignal) {
            emit connectionChanged(false, QString());
        }
        return false;
    }

    serial_.setDataTerminalReady(true);
    serial_.clear(QSerialPort::AllDirections);
    resetMeterParser();
    rxMode_ = RxMode::Meter;
    resetPendingCommand();
    sendWakeAndCommand('S');

    if (announceConnection) {
        emitStatus(QStringLiteral("Connected to %1. Meter streaming enabled.").arg(portName));
    }
    if (emitConnectionSignal) {
        emit connectionChanged(true, portName);
    }
    return true;
}

void LdgTunerController::setBusy(bool busy)
{
    if (busy_ == busy) {
        return;
    }

    busy_ = busy;
    emit busyChanged(busy_);
}

void LdgTunerController::emitStatus(const QString& message, bool error)
{
    emit statusMessage(message, error);
}

void LdgTunerController::resetMeterParser()
{
    meterPayload_.clear();
    eomCount_ = 0;
}

void LdgTunerController::beginSilentMeterReconnect()
{
    if (silentReconnectInProgress_ || rxMode_ != RxMode::Meter || !serial_.isOpen()) {
        resetMeterParser();
        return;
    }

    controlSettleTimer_.stop();
    responseTimer_.stop();
    resetPendingCommand();
    resetMeterParser();
    rxMode_ = RxMode::Meter;
    reconnectPortName_ = serial_.portName();
    silentReconnectInProgress_ = true;
    silentReconnectAttempts_ = 0;

    serial_.clear(QSerialPort::AllDirections);
    serial_.close();
    silentReconnectTimer_.start(kSilentReconnectDelayMs);
}

void LdgTunerController::processMeterByte(char byte)
{
    if (meterPayload_.size() < 6) {
        meterPayload_.append(byte);
        return;
    }

    if (byte == ';') {
        ++eomCount_;
        if (eomCount_ == 2) {
            if (meterPayload_.size() == 6) {
                neoldg::MeterSample sample;
                sample.forwardRaw = (static_cast<quint16>(static_cast<unsigned char>(meterPayload_[0])) << 8)
                    | static_cast<quint16>(static_cast<unsigned char>(meterPayload_[1]));
                sample.reflectedRaw = (static_cast<quint16>(static_cast<unsigned char>(meterPayload_[2])) << 8)
                    | static_cast<quint16>(static_cast<unsigned char>(meterPayload_[3]));
                sample.bandKey = (static_cast<quint16>(static_cast<unsigned char>(meterPayload_[4])) << 8)
                    | static_cast<quint16>(static_cast<unsigned char>(meterPayload_[5]));
                emit meterSampleUpdated(sample);
            }

            resetMeterParser();
        }
        return;
    }

    beginSilentMeterReconnect();
}

void LdgTunerController::sendWakeAndCommand(char command)
{
    if (!serial_.isOpen()) {
        return;
    }

    serial_.write(QByteArray(1, ' '));
    serial_.flush();

    QTimer::singleShot(2, this, [this, command]() {
        if (!serial_.isOpen()) {
            return;
        }
        serial_.write(QByteArray(1, command));
        serial_.flush();
    });
}

void LdgTunerController::startCommand(neoldg::ResponseKind kind, char command, const QString& description)
{
    if (silentReconnectInProgress_) {
        return;
    }

    if (!serial_.isOpen()) {
        emitStatus(QStringLiteral("Connect to a tuner before sending commands."), true);
        return;
    }

    if (busy_) {
        emitStatus(QStringLiteral("The tuner is already processing a command."), true);
        return;
    }

    pendingKind_ = kind;
    pendingCommand_ = command;
    pendingDescription_ = description;
    rxMode_ = RxMode::ControlSettling;
    setBusy(true);

    if (kind == neoldg::ResponseKind::MemoryTune || kind == neoldg::ResponseKind::FullTune) {
        emit tuneOutcomeChanged(neoldg::TuneOutcome::InProgress, QStringLiteral("Tuning"));
    }

    emit logMessage(QStringLiteral("Switching the tuner to control mode for %1.").arg(description));
    sendWakeAndCommand('X');
    controlSettleTimer_.start(kCommandSettleMs);
}

void LdgTunerController::finishCommand(const neoldg::CommandResult& result)
{
    if (result.kind == neoldg::ResponseKind::Bypass && result.success) {
        bypassActive_ = true;
    } else if (result.kind == neoldg::ResponseKind::AutoTune && result.success) {
        bypassActive_ = false;
    }

    if (result.kind == neoldg::ResponseKind::MemoryTune || result.kind == neoldg::ResponseKind::FullTune) {
        neoldg::TuneOutcome outcome = neoldg::TuneOutcome::Error;
        QString label = QStringLiteral("Error");

        switch (result.code.unicode()) {
        case 'T':
            outcome = neoldg::TuneOutcome::Good;
            label = QStringLiteral("Good");
            break;
        case 'M':
            outcome = neoldg::TuneOutcome::Okay;
            label = QStringLiteral("Okay");
            break;
        case 'F':
            outcome = neoldg::TuneOutcome::Fail;
            label = QStringLiteral("Fail");
            break;
        case 'E':
        default:
            outcome = neoldg::TuneOutcome::Error;
            label = QStringLiteral("Error");
            break;
        }

        emit tuneOutcomeChanged(outcome, label);
    }

    emit commandCompleted(result);
    setBusy(false);
    returnToMeterMode();
    resetPendingCommand();
}

void LdgTunerController::returnToMeterMode()
{
    if (!serial_.isOpen()) {
        return;
    }

    QTimer::singleShot(kReturnToMeterDelayMs, this, [this]() {
        if (!serial_.isOpen()) {
            return;
        }

        rxMode_ = RxMode::Meter;
        resetMeterParser();
        sendWakeAndCommand('S');
        emit logMessage(QStringLiteral("Meter streaming resumed."));
    });
}

void LdgTunerController::resetPendingCommand()
{
    pendingKind_ = neoldg::ResponseKind::Unknown;
    pendingCommand_ = '\0';
    pendingDescription_.clear();
}
